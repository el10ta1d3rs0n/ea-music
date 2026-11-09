#define _GNU_SOURCE
#include "ytdlp.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DURATION_UNKNOWN "--:--"
#define DURATION_CHUNK 5
#define ID_SIZE (sizeof(((SongResult *)0)->id))
#define NORMALIZE_FILTER "lavfi=[loudnorm=I=-16:TP=-1.5:LRA=11]"
#define MAX_WORKERS 8

static pthread_mutex_t results_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool searching = false;
static SongResult cached_results[MAX_RESULTS];
static int results_count = 0;
static unsigned search_gen = 0;
static pid_t worker_pids[MAX_WORKERS];

static pid_t current_player_pid = -1;

/* ---- mpv IPC state (used from the main/UI thread only) ---- */
static int ipc_fd = -1;
static char ipc_path[108] = {0};
static double cached_time = -1.0;
static double cached_duration = -1.0;
static long long last_poll_ms = 0;
static unsigned request_counter = 0;
static bool paused = false;
static int volume = YTDLP_VOLUME_DEFAULT; /* 0-200, starts at 120 every launch */
static bool volume_dirty = false;         /* must still be pushed to mpv */
static bool normalize = false;            /* audio normalization, default OFF */
static bool normalize_dirty = false;

static bool loop_song = false;   /* repeat current song, default OFF */
static bool loop_dirty = false;

/* NEW: debounced persistence of volume to settings.conf.
 * Set by YTDLP_SetVolume, flushed from YTDLP_GetVolume once the value
 * has been stable for SETTINGS_SAVE_DEBOUNCE_MS. Prevents writing
 * the file 60x per second while the user drags the volume slider. */
static bool settings_dirty = false;
static long long settings_touched_ms = 0;
#define SETTINGS_SAVE_DEBOUNCE_MS 500

#define IPC_POLL_INTERVAL_MS 250
#define IPC_TIMEOUT_MS 100

typedef struct {
    char query[256];
} SearchParams;

typedef struct {
    unsigned gen;
    int count;
    char ids[DURATION_CHUNK][ID_SIZE];
} DurationJob;

static bool IsSafeVideoId(const char *id)
{
    if (!id || !id[0])
        return false;

    for (const unsigned char *p = (const unsigned char *)id; *p; p++) {
        if (!( (*p >= 'a' && *p <= 'z') ||
               (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') ||
               *p == '_' || *p == '-')) {
            return false;
        }
    }

    return true;
}

/* Bounded copy that always terminates and never triggers truncation warnings. */
static void CopyStr(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;

    size_t n = src ? strnlen(src, dst_size - 1) : 0;
    if (n)
        memcpy(dst, src, n);
    dst[n] = '\0';
}

static void WaitForChild(pid_t pid)
{
    if (pid <= 0)
        return;

    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
}

/* ------------------------------------------------------------------ */
/* Settings (saved to ~/.config/ea-music/settings.conf)                */
/*   normalize=0|1                                                     */
/*   volume=0..YTDLP_VOLUME_MAX    (NEW)                               */
/* ------------------------------------------------------------------ */

static bool SettingsPaths(char *dir, size_t dir_size,
                          char *file, size_t file_size, bool create)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");

    if (xdg && xdg[0]) {
        snprintf(dir, dir_size, "%s/ea-music", xdg);
    } else if (home && home[0]) {
        if (create) {
            char parent[512];
            snprintf(parent, sizeof(parent), "%s/.config", home);
            mkdir(parent, 0755);
        }
        snprintf(dir, dir_size, "%s/.config/ea-music", home);
    } else {
        return false;
    }

    if (create)
        mkdir(dir, 0755);

    snprintf(file, file_size, "%s/settings.conf", dir);
    return true;
}

static void LoadSettings(void)
{
    char dir[512], file[600];
    if (!SettingsPaths(dir, sizeof(dir), file, sizeof(file), false))
        return;

    FILE *fp = fopen(file, "r");
    if (!fp)
        return;

    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        int value;
        if (sscanf(line, "normalize=%d", &value) == 1) {
            normalize = value != 0;
        } else if (sscanf(line, "volume=%d", &value) == 1) {
            /* NEW: restore the last-used volume, clamped to a sane range. */
            if (value < 0) value = 0;
            if (value > YTDLP_VOLUME_MAX) value = YTDLP_VOLUME_MAX;
            volume = value;
        }
    }

    fclose(fp);
}

static void SaveSettings(void)
{
    char dir[512], file[600];
    if (!SettingsPaths(dir, sizeof(dir), file, sizeof(file), true))
        return;

    FILE *fp = fopen(file, "w");
    if (!fp)
        return;

    fprintf(fp, "normalize=%d\n", normalize ? 1 : 0);
    fprintf(fp, "volume=%d\n", volume);      /* NEW */

    fclose(fp);

    settings_dirty = false;                  /* NEW: this write caught us up */
}

/* ------------------------------------------------------------------ */
/* Search (YouTube Music)                                              */
/* ------------------------------------------------------------------ */

static void UrlEncode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;

    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        bool safe = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                    (*p >= '0' && *p <= '9') ||
                    *p == '-' || *p == '_' || *p == '.' || *p == '~';
        size_t need = safe ? 1 : 3;

        if (n + need + 1 > dst_size)
            break;

        if (safe) {
            dst[n++] = (char)*p;
        } else {
            dst[n++] = '%';
            dst[n++] = hex[*p >> 4];
            dst[n++] = hex[*p & 15];
        }
    }

    dst[n] = '\0';
}

static void FormatDuration(double seconds, char *out, size_t size)
{
    long t = (long)(seconds + 0.5);
    if (t <= 0) {
        snprintf(out, size, "%s", DURATION_UNKNOWN);
        return;
    }

    long h = t / 3600, m = (t % 3600) / 60, s = t % 60;
    if (h > 0)
        snprintf(out, size, "%ld:%02ld:%02ld", h, m, s);
    else
        snprintf(out, size, "%ld:%02ld", m, s);
}

static FILE *OpenSearchPipe(const char *query, pid_t *child_pid)
{
    char encoded[sizeof(((SearchParams *)0)->query) * 3 + 1];
    char search_url[sizeof(encoded) + 64];
    char limit[16];

    UrlEncode(query, encoded, sizeof(encoded));
    snprintf(search_url, sizeof(search_url),
             "https://music.youtube.com/search?q=%s#songs", encoded);
    snprintf(limit, sizeof(limit), "%d", MAX_RESULTS);

    int pipefd[2];

    if (pipe(pipefd) != 0)
        return NULL;

    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NULL;
    }

    if (pid == 0) {
        close(pipefd[0]);

        if (dup2(pipefd[1], STDOUT_FILENO) < 0)
            _exit(127);

        close(pipefd[1]);

        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }

        execlp("yt-dlp", "yt-dlp",
               "--flat-playlist",
               "--skip-download",
               "--no-warnings",
               "--socket-timeout", "10",
               "--playlist-end", limit,
               "--print",
               "%(id)s\t%(title)s\t%(duration)s",
               search_url,
               (char *)NULL);

        _exit(127);
    }

    close(pipefd[1]);

    FILE *stream = fdopen(pipefd[0], "r");
    if (!stream) {
        close(pipefd[0]);
        kill(pid, SIGTERM);
        WaitForChild(pid);
        return NULL;
    }

    *child_pid = pid;
    return stream;
}

/* ---- background duration lookups (cancelled when a song is played) ---- */

static void RegisterWorker(pid_t pid)
{
    pthread_mutex_lock(&results_mutex);
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (worker_pids[i] == 0) {
            worker_pids[i] = pid;
            break;
        }
    }
    pthread_mutex_unlock(&results_mutex);
}

static void UnregisterWorker(pid_t pid)
{
    pthread_mutex_lock(&results_mutex);
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (worker_pids[i] == pid)
            worker_pids[i] = 0;
    }
    pthread_mutex_unlock(&results_mutex);
}

static void CancelDurationWorkers(void)
{
    pthread_mutex_lock(&results_mutex);
    for (int i = 0; i < MAX_WORKERS; i++) {
        if (worker_pids[i] > 0)
            kill(worker_pids[i], SIGTERM);
    }
    pthread_mutex_unlock(&results_mutex);
}

static void *DurationWorker(void *arg)
{
    DurationJob *job = arg;

    static const char *fixed[] = {
        "yt-dlp", "--skip-download", "--no-warnings", "--ignore-errors",
        "--no-playlist", "--socket-timeout", "10",
        "--print", "%(id)s\t%(duration)s"
    };
    enum { FIXED_COUNT = sizeof(fixed) / sizeof(fixed[0]) };

    char urls[DURATION_CHUNK][ID_SIZE + 48];
    const char *argv[FIXED_COUNT + DURATION_CHUNK + 1];
    int n = 0;

    for (int i = 0; i < FIXED_COUNT; i++)
        argv[n++] = fixed[i];

    for (int i = 0; i < job->count; i++) {
        snprintf(urls[i], sizeof(urls[i]),
                 "https://music.youtube.com/watch?v=%s", job->ids[i]);
        argv[n++] = urls[i];
    }
    argv[n] = NULL;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        free(job);
        return NULL;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        free(job);
        return NULL;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }

        execvp("yt-dlp", (char *const *)argv);
        _exit(127);
    }

    close(pipefd[1]);
    RegisterWorker(pid);

    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        close(pipefd[0]);
        kill(pid, SIGTERM);
        UnregisterWorker(pid);
        WaitForChild(pid);
        free(job);
        return NULL;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';

        char *tab = strchr(line, '\t');
        if (!tab)
            continue;

        *tab = '\0';
        const char *id = line;
        const char *dur = tab + 1;

        char *end = NULL;
        double seconds = strtod(dur, &end);
        if (end == dur)
            continue; /* "NA" */

        char formatted[32];
        FormatDuration(seconds, formatted, sizeof(formatted));

        bool stale = false;

        pthread_mutex_lock(&results_mutex);
        if (job->gen != search_gen) {
            stale = true;
        } else {
            for (int i = 0; i < results_count; i++) {
                if (strcmp(cached_results[i].id, id) == 0) {
                    CopyStr(cached_results[i].duration,
                            sizeof(cached_results[i].duration), formatted);
                    break;
                }
            }
        }
        pthread_mutex_unlock(&results_mutex);

        if (stale) {
            kill(pid, SIGTERM); /* a newer search replaced these results */
            break;
        }
    }

    fclose(fp);
    UnregisterWorker(pid);
    WaitForChild(pid);
    free(job);
    return NULL;
}

static void StartDurationJobs(char ids[][ID_SIZE], int count, unsigned gen)
{
    for (int start = 0; start < count; start += DURATION_CHUNK) {
        DurationJob *job = calloc(1, sizeof(*job));
        if (!job)
            return;

        job->gen = gen;
        job->count = count - start < DURATION_CHUNK
                         ? count - start : DURATION_CHUNK;

        for (int i = 0; i < job->count; i++)
            CopyStr(job->ids[i], ID_SIZE, ids[start + i]);

        pthread_t thread;
        if (pthread_create(&thread, NULL, DurationWorker, job) != 0) {
            free(job);
            return;
        }

        pthread_detach(thread);
    }
}

static void *RunSearchThread(void *arg)
{
    SearchParams *params = arg;
    SongResult temp_results[MAX_RESULTS] = {0};
    int count = 0;

    pid_t child_pid = -1;
    FILE *fp = OpenSearchPipe(params->query, &child_pid);

    if (fp) {
        char line[4096];

        while (count < MAX_RESULTS && fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\r\n")] = '\0';

            char *id = line;
            char *title = strchr(id, '\t');
            if (!title)
                continue;

            *title++ = '\0';

            char *duration = strchr(title, '\t');
            if (!duration)
                continue;

            *duration++ = '\0';

            if (!IsSafeVideoId(id) || !title[0])
                continue;

            SongResult *result = &temp_results[count];

            CopyStr(result->id, sizeof(result->id), id);
            CopyStr(result->title, sizeof(result->title), title);

            char *end = NULL;
            double seconds = strtod(duration, &end);
            if (end != duration && seconds > 0.0) {
                char tmp[32];
                FormatDuration(seconds, tmp, sizeof(tmp));
                CopyStr(result->duration, sizeof(result->duration), tmp);
            } else {
                CopyStr(result->duration, sizeof(result->duration),
                        DURATION_UNKNOWN);
            }

            count++;
        }

        fclose(fp);
        WaitForChild(child_pid);
    }

    char missing[MAX_RESULTS][ID_SIZE];
    int missing_count = 0;
    unsigned gen;

    pthread_mutex_lock(&results_mutex);
    memcpy(cached_results, temp_results, sizeof(temp_results));
    results_count = count;
    searching = false;
    gen = search_gen;

    for (int i = 0; i < count; i++) {
        if (strcmp(cached_results[i].duration, DURATION_UNKNOWN) == 0)
            CopyStr(missing[missing_count++], ID_SIZE, cached_results[i].id);
    }
    pthread_mutex_unlock(&results_mutex);

    free(params);

    if (missing_count > 0)
        StartDurationJobs(missing, missing_count, gen);

    return NULL;
}

void YTDLP_Init(void)
{
    LoadSettings();
}

void YTDLP_SearchAsync(const char *query)
{
    if (!query || !query[0])
        return;

    SearchParams *params = malloc(sizeof(*params));
    if (!params)
        return;

    snprintf(params->query, sizeof(params->query), "%s", query);

    pthread_mutex_lock(&results_mutex);

    if (searching) {
        pthread_mutex_unlock(&results_mutex);
        free(params);
        return;
    }

    searching = true;
    search_gen++;
    results_count = 0;
    memset(cached_results, 0, sizeof(cached_results));

    pthread_mutex_unlock(&results_mutex);

    pthread_t thread;
    if (pthread_create(&thread, NULL, RunSearchThread, params) != 0) {
        pthread_mutex_lock(&results_mutex);
        searching = false;
        pthread_mutex_unlock(&results_mutex);
        free(params);
        return;
    }

    pthread_detach(thread);
}

int YTDLP_GetResults(SongResult *out_results, int max_out)
{
    if (!out_results || max_out <= 0)
        return 0;

    pthread_mutex_lock(&results_mutex);

    int count = results_count < max_out ? results_count : max_out;
    memcpy(out_results, cached_results, (size_t)count * sizeof(*out_results));

    pthread_mutex_unlock(&results_mutex);
    return count;
}

bool YTDLP_IsSearching(void)
{
    pthread_mutex_lock(&results_mutex);
    bool value = searching;
    pthread_mutex_unlock(&results_mutex);
    return value;
}

/* Clear the cached results so the search page falls back to its
 * default "Enter a search and press Enter or Search." state.
 * Used when the user submits an empty search box. */
void YTDLP_ClearResults(void)
{
    /* Don't stomp on a search that's still running — the worker
       would just overwrite cached_results when it finishes. */
    if (YTDLP_IsSearching())
        return;

    /* Kill any in-flight duration lookups so they can't repopulate
       cached_results after we clear. */
    CancelDurationWorkers();

    pthread_mutex_lock(&results_mutex);
    search_gen++;                              /* invalidate stale workers */
    memset(cached_results, 0, sizeof(cached_results));
    results_count = 0;
    pthread_mutex_unlock(&results_mutex);
}

/* ------------------------------------------------------------------ */
/* mpv IPC                                                             */
/* ------------------------------------------------------------------ */

static long long NowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static void IpcClose(void)
{
    if (ipc_fd >= 0) {
        close(ipc_fd);
        ipc_fd = -1;
    }
}

static void IpcReset(void)
{
    IpcClose();

    if (ipc_path[0]) {
        unlink(ipc_path);
        ipc_path[0] = '\0';
    }

    cached_time = -1.0;
    cached_duration = -1.0;
    last_poll_ms = 0;
    paused = false;
}

static bool IpcConnect(void)
{
    if (ipc_fd >= 0)
        return true;

    if (!ipc_path[0])
        return false;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ipc_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return false; /* mpv probably hasn't created the socket yet */
    }

    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = IPC_TIMEOUT_MS * 1000
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    ipc_fd = fd;
    return true;
}

/* Returns true if line contains "request_id":<id> as a whole number. */
static bool LineHasRequestId(const char *line, unsigned id)
{
    char tag[48];
    snprintf(tag, sizeof(tag), "\"request_id\":%u", id);

    const char *p = strstr(line, tag);
    if (!p)
        return false;

    char next = p[strlen(tag)];
    return !(next >= '0' && next <= '9');
}

/*
 * Sends {"command": <command_array>, "request_id": N} and waits for the
 * matching reply line. mpv also emits event lines on the same socket;
 * those are skipped.
 */
static bool IpcRequest(const char *command_array, char *reply, size_t reply_size)
{
    if (!IpcConnect())
        return false;

    unsigned id = ++request_counter;
    char msg[512];
    int len = snprintf(msg, sizeof(msg),
                       "{\"command\":%s,\"request_id\":%u}\n",
                       command_array, id);
    if (len < 0 || (size_t)len >= sizeof(msg))
        return false;

    if (send(ipc_fd, msg, (size_t)len, MSG_NOSIGNAL) != len) {
        IpcClose();
        return false;
    }

    char buf[4096];
    size_t used = 0;

    for (int attempts = 0; attempts < 32; attempts++) {
        ssize_t n = recv(ipc_fd, buf + used, sizeof(buf) - 1 - used, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            IpcClose(); /* timeout, closed, or error: reconnect next time */
            return false;
        }

        used += (size_t)n;
        buf[used] = '\0';

        char *line = buf;
        char *nl;
        while ((nl = strchr(line, '\n')) != NULL) {
            *nl = '\0';

            if (LineHasRequestId(line, id)) {
                if (reply && reply_size > 0)
                    snprintf(reply, reply_size, "%s", line);
                return true;
            }

            line = nl + 1;
        }

        /* Keep any partial trailing line for the next recv. */
        size_t remaining = strlen(line);
        memmove(buf, line, remaining + 1);
        used = remaining;

        if (used >= sizeof(buf) - 1)
            used = 0; /* oversized garbage line, discard */
    }

    return false;
}

static bool IpcGetDouble(const char *property, double *out)
{
    char cmd[96];
    char reply[1024];

    snprintf(cmd, sizeof(cmd), "[\"get_property\",\"%s\"]", property);

    if (!IpcRequest(cmd, reply, sizeof(reply)))
        return false;

    if (!strstr(reply, "\"error\":\"success\""))
        return false;

    const char *p = strstr(reply, "\"data\":");
    if (!p)
        return false;

    p += 7;

    char *end = NULL;
    double value = strtod(p, &end);
    if (end == p)
        return false; /* e.g. "data":null */

    *out = value;
    return true;
}

static bool IpcApplyVolume(void)
{
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "[\"set_property\",\"volume\",%d]", volume);
    return IpcRequest(cmd, NULL, 0);
}

static bool IpcApplyNormalize(void)
{
    if (normalize)
        return IpcRequest("[\"af\",\"set\",\"" NORMALIZE_FILTER "\"]", NULL, 0);

    return IpcRequest("[\"af\",\"clear\",\"\"]", NULL, 0);
}

static bool IpcApplyLoop(void)
{
    return IpcRequest(loop_song
        ? "[\"set_property\",\"loop-file\",\"inf\"]"
        : "[\"set_property\",\"loop-file\",\"no\"]", NULL, 0);
}

static void PollPlayback(void)
{
    long long now = NowMs();
    if (now - last_poll_ms < IPC_POLL_INTERVAL_MS)
        return;

    last_poll_ms = now;

    /* Settings changed before mpv's socket existed: retry until they land. */
    if (volume_dirty && IpcApplyVolume())
        volume_dirty = false;

    if (normalize_dirty && IpcApplyNormalize())
        normalize_dirty = false;

    if (loop_dirty && IpcApplyLoop())
        loop_dirty = false;

    double value;

    if (IpcGetDouble("time-pos", &value))
        cached_time = value;

    if (IpcGetDouble("duration", &value))
        cached_duration = value;
}

/* ------------------------------------------------------------------ */
/* Playback                                                            */
/* ------------------------------------------------------------------ */

/* Finds `name` in PATH. */
static bool FindInPath(const char *name, char *out, size_t size)
{
    const char *path = getenv("PATH");
    if (!path)
        return false;

    char copy[4096];
    CopyStr(copy, sizeof(copy), path);

    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir;
         dir = strtok_r(NULL, ":", &save)) {
        snprintf(out, size, "%s/%s", dir, name);
        if (access(out, X_OK) == 0)
            return true;
    }

    return false;
}

/*
 * Creates <private dir>/ea-music -> mpv so the process is listed as
 * "ea-music" in task managers. Falls back to plain "mpv" on any failure.
 */
static bool PreparePlayerLink(char *link, size_t size)
{
    char mpv[1024];
    if (!FindInPath("mpv", mpv, sizeof(mpv)))
        return false;

    char dir[300];
    const char *runtime = getenv("XDG_RUNTIME_DIR");

    if (runtime && runtime[0])
        snprintf(dir, sizeof(dir), "%s/ea-music", runtime);
    else
        snprintf(dir, sizeof(dir), "/tmp/ea-music-%d", (int)getuid());

    mkdir(dir, 0700);

    struct stat st;
    if (lstat(dir, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != getuid())
        return false; /* not ours: don't use it */

    snprintf(link, size, "%s/ea-music", dir);
    unlink(link);

    return symlink(mpv, link) == 0;
}

void YTDLP_PlaySong(const char *id)
{
    if (!IsSafeVideoId(id))
        return;

    /* Free the network/CPU for the track the user just picked. */
    CancelDurationWorkers();

    YTDLP_StopPlayback();

    /* Prepare everything before forking. */
    snprintf(ipc_path, sizeof(ipc_path), "/tmp/ytdlp_mpv_%d.sock", (int)getpid());
    unlink(ipc_path);

    char ipc_arg[160];
    snprintf(ipc_arg, sizeof(ipc_arg), "--input-ipc-server=%s", ipc_path);

    char vol_arg[32];
    snprintf(vol_arg, sizeof(vol_arg), "--volume=%d", volume);

    char volmax_arg[32];
    snprintf(volmax_arg, sizeof(volmax_arg), "--volume-max=%d", YTDLP_VOLUME_MAX);

    char url[256];
    int length = snprintf(url, sizeof(url),
                          "https://music.youtube.com/watch?v=%s", id);
    if (length < 0 || (size_t)length >= sizeof(url)) {
        ipc_path[0] = '\0';
        return;
    }

    const char *argv[28];
    int n = 0;
    argv[n++] = "ea-music";
    argv[n++] = "--no-config";
    argv[n++] = "--no-video";
    argv[n++] = "--vid=no";
    argv[n++] = "--audio-display=no";
    argv[n++] = "--really-quiet";
    argv[n++] = "--no-terminal";
    argv[n++] = "--replaygain=no";
    argv[n++] = "--ytdl-format=bestaudio/best";
    argv[n++] = "--ytdl-raw-options=no-playlist=,no-warnings=";
    argv[n++] = "--cache=yes";
    argv[n++] = "--demuxer-readahead-secs=10";
    argv[n++] = volmax_arg;
    argv[n++] = vol_arg;
    argv[n++] = ipc_arg;
    argv[n++] = "--audio-client-name=ea-music";
    argv[n++] = "--title=ea-music";
    argv[n++] = "--force-media-title=ea-music";
    if (loop_song)
        argv[n++] = "--loop-file=inf";
    if (normalize)
        argv[n++] = "--af=" NORMALIZE_FILTER;
    argv[n++] = url;
    argv[n] = NULL;

    char player_link[400];
    bool use_link = PreparePlayerLink(player_link, sizeof(player_link));

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork for mpv");
        ipc_path[0] = '\0';
        return;
    }

    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            close(nullfd);
        }

        prctl(PR_SET_PDEATHSIG, SIGTERM); /* die with the app */

        if (use_link)
            execv(player_link, (char *const *)argv);

        argv[0] = "mpv";
        execvp("mpv", (char *const *)argv);
        _exit(127);
    }

    current_player_pid = pid;
    cached_time = -1.0;
    cached_duration = -1.0;
    last_poll_ms = 0;
    paused = false;
    volume_dirty = false;    /* mpv starts with the right volume */
    normalize_dirty = false; /* ...and the right filter */
    loop_dirty = false;      /* ...and the right loop mode */
}

void YTDLP_StopPlayback(void)
{
    if (current_player_pid > 0) {
        pid_t pid = current_player_pid;
        current_player_pid = -1;

        if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
            perror("stop mpv");

        WaitForChild(pid);
    }

    IpcReset();

    /* NEW: commit any pending volume change now — after stop, the UI
     * won't poll YTDLP_GetVolume() again until a new track starts, so
     * the debounce in GetVolume would never fire on its own. */
    if (settings_dirty)
        SaveSettings();
}

bool YTDLP_IsPlaying(void)
{
    if (current_player_pid <= 0)
        return false;

    int status;
    pid_t result = waitpid(current_player_pid, &status, WNOHANG);

    if (result == current_player_pid ||
        (result < 0 && errno == ECHILD)) {
        current_player_pid = -1;
        IpcReset();
        return false;
    }

    return true;
}

/* Current position in seconds, or -1.0 if unknown. */
double YTDLP_GetTime(void)
{
    if (!YTDLP_IsPlaying())
        return -1.0;

    PollPlayback();
    return cached_time;
}

/* Total length in seconds, or -1.0 if unknown. */
double YTDLP_GetDuration(void)
{
    if (!YTDLP_IsPlaying())
        return -1.0;

    PollPlayback();
    return cached_duration;
}

/* Absolute seek to `seconds` from the start of the track. */
void YTDLP_Seek(double seconds)
{
    if (!YTDLP_IsPlaying())
        return;

    if (seconds < 0.0)
        seconds = 0.0;

    if (cached_duration > 0.0 && seconds > cached_duration)
        seconds = cached_duration;

    /* Format as integer milliseconds to stay locale-independent. */
    long long ms = (long long)(seconds * 1000.0 + 0.5);
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "[\"seek\",%lld.%03lld,\"absolute\"]",
             ms / 1000, ms % 1000);

    if (IpcRequest(cmd, NULL, 0)) {
        cached_time = seconds; /* update UI immediately */
        last_poll_ms = NowMs();
    }
}

void YTDLP_TogglePause(void)
{
    if (!YTDLP_IsPlaying())
        return;

    bool target = !paused;
    char cmd[96];
    snprintf(cmd, sizeof(cmd), "[\"set_property\",\"pause\",%s]",
             target ? "true" : "false");

    if (IpcRequest(cmd, NULL, 0))
        paused = target;
}

bool YTDLP_IsPaused(void)
{
    return YTDLP_IsPlaying() && paused;
}

void YTDLP_SetVolume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > YTDLP_VOLUME_MAX) percent = YTDLP_VOLUME_MAX;

    if (percent == volume && !volume_dirty)
        return;

    volume = percent;

    if (YTDLP_IsPlaying())
        volume_dirty = !IpcApplyVolume();

    /* NEW: remember that disk is out of date. GetVolume flushes
     * this after the value has been stable for a short while,
     * so dragging the slider doesn't write the file every frame. */
    settings_dirty = true;
    settings_touched_ms = NowMs();
}

int YTDLP_GetVolume(void)
{
    /* NEW: debounced save. UIDraw calls this at least once per frame,
     * so a drag that just ended will land on disk within ~500ms. */
    if (settings_dirty &&
        NowMs() - settings_touched_ms > SETTINGS_SAVE_DEBOUNCE_MS) {
        SaveSettings();
    }

    return volume;
}

void YTDLP_SetNormalize(bool enabled)
{
    if (enabled == normalize)
        return;

    normalize = enabled;

    if (YTDLP_IsPlaying())
        normalize_dirty = !IpcApplyNormalize();

    SaveSettings();
}

bool YTDLP_GetNormalize(void)
{
    return normalize;
}

void YTDLP_SetLoop(bool enabled)
{
    if (enabled == loop_song)
        return;

    loop_song = enabled;

    if (YTDLP_IsPlaying())
        loop_dirty = !IpcApplyLoop();
}

bool YTDLP_GetLoop(void)
{
    return loop_song;
}

void YTDLP_Cleanup(void)
{
    CancelDurationWorkers();
    YTDLP_StopPlayback();
    SaveSettings();
}

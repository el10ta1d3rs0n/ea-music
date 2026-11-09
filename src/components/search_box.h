#ifndef SEARCH_BOX_H
#define SEARCH_BOX_H

#include <stdbool.h>

void SearchBoxUpdate(void);
const char *SearchBoxText(void);
void SearchBoxDraw(int screenWidth);
bool SearchBoxIsSubmitted(void);
void SearchBoxClearSubmitted(void);

#endif

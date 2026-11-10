// studio_marks.h -- the Marks page of the Looks workspace (studio_marks.cpp): types.json's
// "marks", the looks a mod's scripts may put on a mark the engine draws on the ground
// (docs/mod-data.md, "The looks a mark wears").
#ifndef STUDIO_MARKS_H
#define STUDIO_MARKS_H

#include <string>
#include <vector>

// The looks the open mod declares, in the order types.json has them.
std::vector<std::string> StudioMarkNames(void);
// The look's page, drawn at the cursor fW wide, as the Looks workspace's other pages are.
void StudioMarksPage(const std::string &name, float fW);
// The Marks section of the nav: what each row says of a look, its blocks counted.
std::string StudioMarkNote(const std::string &name);
// Its commands. The mark in the canvas game is set again by the page itself while it is drawn.
void StudioMarksInit(void);

#endif

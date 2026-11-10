// port_typenames.h -- the original game's rows by name, for every table a mod's data adds to
// or edits (port_typenames.cpp; docs/mod-data.md): a mod names a row as "terran_marine" or
// "gauss_rifle" rather than by its number.
#ifndef PORT_TYPENAMES_H
#define PORT_TYPENAMES_H

// The original's row a name gives in a table ("units", "weapons", "flingy", "sprites",
// "images", "orders", "techs", "upgrades", "sounds" or "portraits"), or -1 when it names none. A table's "none" row
// is named where it has one (units 228, orders 189).
int TypeNameFind(const char *pszTable, const char *pszName);

// The other way: the name of row n of a table, the first a row of two names has; null when
// the table has no such row. A mode's generate reads a row by name and names its base by name,
// so the row it is given carries the names of the types it points at.
const char *TypeNameOf(const char *pszTable, int n);

#endif

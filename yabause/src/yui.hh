#include "memory.hh"

/* this function should be the "main" function of the program,
 * the parameter is a function pointer, this function wants a
 * SaturnMemory as parameter, the interface must create it
 * (ie: saturn = new SaturnMemory();) after the user has choose
 * the bios file and all others possible configurations.
 * The function should be called as often as possible */
void	yui_init(int (*)(void *));

/* quit yui_init */
void	yui_quit(void);

/* display the frame per secound, can be ignored */
void	yui_fps(int);

/* hide or show the interface */
void	yui_hide_show(void);

/* returns a non NULL BIOS filename */
char *	yui_bios(void);

/* returns a cd device name or NULL if no cd device is used */
char *	yui_cdrom(void);
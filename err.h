#pragma once

// This file was provided to use as a utility in this project.
// I did not write it.

// error code for when we try to move a parent into a child
#define EILLEGALMOVE -1

/* wypisuje informacje o błędnym zakończeniu funkcji systemowej
i kończy działanie */
void syserr(int bl, const char *fmt, ...);

/* wypisuje informacje o błędzie i kończy działanie */
extern void fatal(const char* fmt, ...);

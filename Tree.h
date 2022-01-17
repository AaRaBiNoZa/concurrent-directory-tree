#pragma once

#include "HashMap.h"
#include "Synchro.h"

typedef struct Tree Tree; // Let "Tree" mean the same as "struct Tree".

struct Tree {
  char *name;
  struct Synchro synchronizer;
  HashMap *children; // values are of type Tree*
};

/**
 * Destroys a given node.
 */
int tree_destroy(Tree *tree);

/**
 * Creates a new directory tree with one empty folder - "/"
 */
Tree* tree_new();

/**
 * Frees all memory taken by this tree and its subtrees.
 */
void tree_free(Tree*);

/**
 * Returns a c-string representing the contents of a given directory.
 * (names of sub-folders separated by commas)
 * Freeing the result's memory is a responsibility of the caller.
 */
char* tree_list(Tree* tree, const char* path);

/**
 * Creates a new directory in a given path.
 */
int tree_create(Tree* tree, const char* path);

/**
 * Removes the directory as long as it's empty.
 */
int tree_remove(Tree* tree, const char* path);

/**
 * Moves the directory source with its contents to path specified by target.
 */
int tree_move(Tree* tree, const char* source, const char* target);

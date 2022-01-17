#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "Synchro.h"
#include "Tree.h"
#include "err.h"
#include "path_utils.h"

/**
 * A utility function that receives a pointer to a pointer to a node  (folder)
 * for which the caller must possess reading rights and a VALID path.
 * It tries to set the cur_folder to point to a node specified in path.
 * On success, returns 0 and cur_folder is pointing to a node specified in path.
 * On failure returns ENOENT and cur_folder is pointing to the last node
 * for which the caller now has reading rights. (the caller must surrender
 * those rights)
 * @param cur_folder pointer to a pointer to a node (folder)
 * @param path c-string representing path
 * @return 0 on success, ENOENT on failure
 */
int synchro_get_to_path(Tree **cur_folder, const char *path) {
  Tree *prev_folder = NULL;
  const char *subpath = path;
  char component[MAX_FOLDER_NAME_LENGTH + 1];

  subpath = split_path(subpath, component);
  while (subpath) {
    prev_folder = *cur_folder;
    *cur_folder = hmap_get(prev_folder->children, component);
    if (*cur_folder == NULL) {
      *cur_folder = prev_folder;
      return ENOENT;
    }
    synchro_visit(&((*cur_folder)->synchronizer));
    synchro_leave_after_visiting(&(prev_folder->synchronizer));

    subpath = split_path(subpath, component);
  }

  return 0;
}

int tree_destroy(Tree *tree) {
  free(tree->name);
  free(tree->children);
  synchro_destroy(&(tree->synchronizer));
  free(tree);
  return 0;
}

Tree *tree_new() {
  Tree *result = malloc(sizeof(Tree));
  CHECK_PTR(result);

  result->name = NULL;
  result->children = hmap_new();
  synchro_init(&(result->synchronizer));

  return result;
}

void tree_free(Tree *tree) {
  Tree *value;
  const char *key;
  HashMapIterator it = hmap_iterator(tree->children);
  while (hmap_next(tree->children, &it, &key, (void **)&value)) {
    tree_free(value);
  }

  free(tree->name);
  hmap_free(tree->children);
  synchro_destroy(&(tree->synchronizer));
  free(tree);
}

char *tree_list(Tree *tree, const char *path) {
  if (!is_path_valid(path)) {
    return NULL;
  }

  // claiming root
  synchro_visit(&(tree->synchronizer));
  Tree *cur_folder = tree;

  // getting to destination
  if (synchro_get_to_path(&cur_folder, path) == ENOENT) {
    synchro_leave_after_visiting(&(cur_folder->synchronizer));
    return NULL;
  }

  char *result = make_map_contents_string(cur_folder->children);
  synchro_leave_after_visiting(&(cur_folder->synchronizer));
  return result;
}

int tree_create(Tree *tree, const char *path) {
  if (!is_path_valid(path)) {
    return EINVAL;
  }

  char folder_name[MAX_FOLDER_NAME_LENGTH + 1];

  // getting the name of the folder to make
  const char *subpath = make_path_to_parent(path, folder_name);
  if (subpath == NULL) { // if wants to create "/"
    return EEXIST;
  }
  const char *to_free = subpath; // cause make_path_to_parent copies

  // claiming root
  synchro_visit(&(tree->synchronizer));
  Tree *cur_folder = tree;

  // getting to the needed place in the folder tree
  if (synchro_get_to_path(&cur_folder, subpath) == ENOENT) {
    synchro_leave_after_visiting(&(cur_folder->synchronizer));
    free((void *)to_free);
    return ENOENT;
  }
  free((void *)to_free);

  synchro_change_from_visiting_to_mod(&(cur_folder->synchronizer));

  // if the folder already exists
  if (hmap_get(cur_folder->children, folder_name) != NULL) {
    synchro_leave_after_modifying(&(cur_folder->synchronizer));
    return EEXIST;
  }

  // getting ready to modify
  Tree *new_folder = tree_new();
  new_folder->name = malloc(strlen(folder_name) + 1);
  CHECK_PTR(new_folder->name);

  strcpy(new_folder->name, folder_name);

  hmap_insert(cur_folder->children, folder_name, new_folder);

  synchro_leave_after_modifying(&(cur_folder->synchronizer));

  return 0;
}

int tree_remove(Tree *tree, const char *path) {
  if (!is_path_valid(path)) {
    return EINVAL;
  }

  char folder_name[MAX_FOLDER_NAME_LENGTH + 1];

  const char *subpath = make_path_to_parent(path, folder_name);
  if (subpath == NULL) {
    return EBUSY;
  }
  const char *to_free = subpath;

  // claiming root
  synchro_visit(&(tree->synchronizer));
  Tree *cur_folder = tree;
  Tree *folder_to_delete;

  // getting to my destination
  if (synchro_get_to_path(&cur_folder, subpath) == ENOENT) {
    synchro_leave_after_visiting(&(cur_folder->synchronizer));
    free((void *)to_free);
    return ENOENT;
  }
  free((void *)to_free);
  synchro_change_from_visiting_to_mod(&(cur_folder->synchronizer));

  // folder to delete doesn't exist
  if ((folder_to_delete = hmap_get(cur_folder->children, folder_name)) ==
      NULL) {
    synchro_leave_after_modifying(&(cur_folder->synchronizer));
    return ENOENT;
  }

  synchro_prepare_for_being_removed(&(folder_to_delete->synchronizer));

  if (hmap_size(folder_to_delete->children) == 0) {
    hmap_remove(cur_folder->children, folder_name);
    // without the next two lines helgrind shows errors but they're not
    // necessary
    synchro_modify(&(folder_to_delete->synchronizer));
    synchro_leave_after_modifying(&(folder_to_delete->synchronizer));
    tree_destroy(folder_to_delete);
  } else {
    synchro_leave_after_bad_remove(&(folder_to_delete->synchronizer));
    synchro_leave_after_modifying(&(cur_folder->synchronizer));
    return ENOTEMPTY;
  }

  synchro_leave_after_modifying(&(cur_folder->synchronizer));

  return 0;
}

/**
 * Utility function that computes the number of nodes from root to lca
 * @param first first path
 * @param second second path
 * @return number of nodes (folders) from root to lca
 */
int get_lca_path_length(const char *first, const char *second) {
  char component1[MAX_FOLDER_NAME_LENGTH + 1];
  char component2[MAX_FOLDER_NAME_LENGTH + 1];
  const char *subpath1 = first;
  const char *subpath2 = second;

  int how_many_dirs_without_base = 0;
  while ((subpath1 = split_path(subpath1, component1)) &&
         ((subpath2 = split_path(subpath2, component2))) &&
         strcmp(component1, component2) == 0) {
    how_many_dirs_without_base++;
  }

  return how_many_dirs_without_base;
}

int tree_move(Tree *tree, const char *source, const char *target) { // TODO
  if (!is_path_valid(source) || !is_path_valid(target)) {
    return EINVAL;
  }
  if (strcmp(source, "/") == 0) {
    return EBUSY;
  }
  if (strcmp(target, "/") == 0) {
    return EEXIST;
  }
  if (strncmp(source, target, strlen(source)) == 0) {
    return EILLEGALMOVE;
  }

  bool is_father_dest_the_lca = false;
  bool is_father_source_the_lca = false;

  char component[MAX_FOLDER_NAME_LENGTH + 1];
  const char *subpath;
  char new_name[MAX_FOLDER_NAME_LENGTH + 1];
  char to_move[MAX_FOLDER_NAME_LENGTH + 1];
  Tree *cur_folder = tree;
  Tree *lca = tree;
  Tree *dest_folder = tree;
  Tree *prev_folder = NULL;
  const char *to_free1, *to_free2;

  // getting to lca
  synchro_visit(&(tree->synchronizer));
  // for sure can't be null since they can't be "/"
  target = make_path_to_parent(target, new_name);
  source = make_path_to_parent(source, to_move);
  subpath = split_path(source, component);
  to_free1 = source;
  to_free2 = target;
  int lca_path = get_lca_path_length(source, target);

  // setting cur folder as their lca
  // source and target with path to lca cut from beginning
  while (subpath && lca_path > 0) {
    lca_path--;
    prev_folder = lca;
    lca = hmap_get(prev_folder->children, component);
    // if any of the parents don't exist
    if (lca == NULL) {
      synchro_leave_after_visiting(&(prev_folder->synchronizer));
      free((void *)to_free1);
      free((void *)to_free2);
      return ENOENT;
    }
    synchro_visit(&(lca->synchronizer));
    synchro_leave_after_visiting(&(prev_folder->synchronizer));
    source = split_path(source, component);
    target = split_path(target, component);
    subpath = split_path(subpath, component);
  }

  // here lca is the lca, source and target are paths from lca
  *component = '\0';

  synchro_change_from_visiting_to_mod(&(lca->synchronizer));

  dest_folder = lca;

  // here extracting the name of folder to "create"
  subpath = split_path(target, component);

  if (subpath == NULL) {
    is_father_dest_the_lca = true;
  } else {
    // if father of dest is  not the lca, then we go to father of dest
    prev_folder = lca;
    dest_folder = hmap_get(dest_folder->children, component);
    if (dest_folder == NULL) {
      synchro_leave_after_modifying(&(lca->synchronizer));
      free((void *)to_free1);
      free((void *)to_free2);
      return ENOENT;
    }
    synchro_visit(&(dest_folder->synchronizer));

    if (synchro_get_to_path(&dest_folder, subpath) == ENOENT) {
      free((void *)to_free1);
      free((void *)to_free2);
      synchro_leave_after_modifying(&(lca->synchronizer));
      synchro_leave_after_visiting(&(dest_folder->synchronizer));
      return ENOENT;
    }
    // target exists and lca is not father of dest
  }

  // if target exists and lca is the father of dest
  if (hmap_get(dest_folder->children, new_name) != NULL) {
    synchro_leave_after_modifying(&(lca->synchronizer));
    if (!is_father_dest_the_lca) {
      synchro_leave_after_visiting(&(dest_folder->synchronizer));
    }
    free((void *)to_free1);
    free((void *)to_free2);
    return EEXIST;
  }

  cur_folder = lca;
  subpath = split_path(source, component);
  if (subpath == NULL) {
    is_father_source_the_lca = true;
  } else {
    cur_folder = hmap_get(cur_folder->children, component);

    if (cur_folder == NULL) {
      synchro_leave_after_modifying(&(lca->synchronizer));
      if (!is_father_dest_the_lca) {
        synchro_leave_after_visiting(&(dest_folder->synchronizer));
      }
      free((void *)to_free1);
      free((void *)to_free2);
      return ENOENT;
    }
    synchro_visit(&(cur_folder->synchronizer));

    if (synchro_get_to_path(&cur_folder, subpath) == ENOENT) {
      synchro_leave_after_modifying(&(lca->synchronizer));
      if (!is_father_dest_the_lca) {
        synchro_leave_after_visiting(&(dest_folder->synchronizer));
      }
      synchro_leave_after_visiting(&(cur_folder->synchronizer));
      free((void *)to_free1);
      free((void *)to_free2);
      return ENOENT;
    }
  }

  // would have to be all are lca
  if (cur_folder != dest_folder) {
    if (!is_father_dest_the_lca) {
      synchro_change_from_visiting_to_mod(&(dest_folder->synchronizer));
    }
    if (!is_father_source_the_lca) {
      synchro_change_from_visiting_to_mod(&(cur_folder->synchronizer));
    }
  }

  if (hmap_get(cur_folder->children, to_move) == NULL) {
    synchro_leave_after_modifying(&(lca->synchronizer));
    if (!is_father_dest_the_lca) {
      synchro_leave_after_modifying(&(dest_folder->synchronizer));
    }
    if (!is_father_source_the_lca) {
      synchro_leave_after_modifying(&(cur_folder->synchronizer));
    }

    free((void *)to_free1);
    free((void *)to_free2);
    return ENOENT;
  }

  Tree *child = hmap_get(cur_folder->children, to_move);
  hmap_remove(cur_folder->children, child->name);
  synchro_modify(&(child->synchronizer));
  strcpy(child->name, new_name);
  hmap_insert(dest_folder->children, child->name, child);
  synchro_leave_after_modifying(&(child->synchronizer));
  synchro_leave_after_modifying(&(lca->synchronizer));
  if (!is_father_dest_the_lca) {
    synchro_leave_after_modifying(&(dest_folder->synchronizer));
  }
  if (!is_father_source_the_lca) {
    synchro_leave_after_modifying(&(cur_folder->synchronizer));
  }
  free((void *)to_free1);
  free((void *)to_free2);
  return 0;
}

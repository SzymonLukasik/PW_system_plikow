#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "Tree.h"
#include "TreeSeq.h"
#include "path_utils.h"
#include "HashMap.h"
#include "safealloc.h"

struct TreeSeq {
    const char* name;
    HashMap* dirs;
    Tree* tree;
};

char* get_root_name() {
    char* name =safe_malloc(2);
    name[0] = '/';
    name[1] = '\0';
    return name;
}

void tree_seq_print_helper(TreeSeq* tree, char* path, size_t path_len) {
    const char* key;
    void* kid;
    HashMapIterator it = hmap_iterator(tree->dirs);
    int count = 0;
    while (hmap_next(tree->dirs, &it, &key, &kid)) {
        count++;
        char* new_path = malloc(MAX_PATH_LENGTH  + 1);
        strncpy(new_path, path, path_len);
        size_t name_len = strlen(((TreeSeq*) kid)->name);
        const char* name = ((TreeSeq*) kid)->name;
        strncpy(new_path + path_len, name, name_len);
        new_path[path_len + name_len] = '/';
        new_path[path_len + name_len + 1] = '\0';
        tree_seq_print_helper((TreeSeq*) kid, new_path, path_len + name_len + 1);
    }

    if (count == 0) {
        fprintf(stderr, "%s\n", path);
    }
    free(path);
}


void tree_seq_print(TreeSeq* tree) {
    char* path = get_root_name();
    tree_seq_print_helper(tree, path, 1);
    fprintf(stderr, "\n");
}

TreeSeq* tree_seq_new(Tree* tree) {
    TreeSeq* tree_seq = safe_malloc(sizeof(TreeSeq));
    tree_seq->name = get_root_name();
    tree_seq->dirs = hmap_new();
    tree_seq->tree = tree;
    return tree_seq;
}

TreeSeq* tree_seq_new_dir(const char* name) {
    TreeSeq* tree = safe_malloc(sizeof(TreeSeq));
    tree->name = name;
    tree->dirs = hmap_new();
    return tree;
}

void tree_seq_free(TreeSeq* tree) {
    const char* key;
    void* kid;
    HashMapIterator it = hmap_iterator(tree->dirs);
    while (hmap_next(tree->dirs, &it, &key, &kid)) {
        hmap_remove(tree->dirs, ((TreeSeq*) kid)->name);
        tree_seq_free(kid);
    }
    free((char*) tree->name);
    hmap_free(tree->dirs);
    free(tree);
}

TreeSeq* tree_seq_get_subdir(TreeSeq* tree, const char* subpath) {
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    while ((subpath = split_path(subpath, component))) {
        tree = hmap_get(tree->dirs, component);
        if (tree == NULL) return NULL;
    }
    return tree;
}

char* tree_seq_list(TreeSeq* tree, const char* path) {
    if (!is_path_valid(path)) return NULL;
    TreeSeq* subdir = tree_seq_get_subdir(tree, path);
    if (subdir == NULL) return NULL;
    char* s = make_map_contents_string(subdir->dirs);
    return s;
}

int get_parent(TreeSeq* tree, const char* path, TreeSeq** parent, char** name) {
    if (!is_path_valid(path)) return EINVAL;
    char* component = safe_malloc(MAX_FOLDER_NAME_LENGTH + 1);
    char* parent_path = make_path_to_parent(path, component);
    if (parent_path == NULL) { // path is "/".
        free(component);
        return EBUSY;
    }
    *parent = tree_seq_get_subdir(tree, parent_path);
    free(parent_path);
    if (*parent == NULL) {
        free(component);
        return ENOENT;
    }
    *name = component;
    return 0;
}

bool is_root_path(const char* path) {
    return (path[0] == '/' && path[1] == '\0');
}

int tree_seq_create(TreeSeq* tree, const char* path) {
    TreeSeq* parent;
    char* name;
    int err;
    if (is_root_path(path)) return EEXIST; // get_parent would return EBUSY.
    if ((err = get_parent(tree, path, &parent, &name)) != 0) return err;
    if (hmap_get(parent->dirs, name) != NULL) {
        free(name);
        return EEXIST;
    }
    TreeSeq* new_dir = tree_seq_new_dir(name);
    hmap_insert(parent->dirs, name, new_dir);
    return 0;
}

int split(TreeSeq* tree, const char* path, TreeSeq** splitted, TreeSeq** _parent) {
    if (is_root_path(path)) return EBUSY;
    TreeSeq* parent;
    char* name;
    int err;
    if ((err = get_parent(tree, path, &parent, &name)) != 0) return err;
    if ((*splitted = hmap_get(parent->dirs, name)) == NULL) {
        free(name);
        return ENOENT;
    }
    hmap_remove(parent->dirs, name);
    free(name);
    *_parent = parent;
    return 0;
}

int tree_seq_remove(TreeSeq* tree, const char* path) {
    TreeSeq* target;
    TreeSeq* parent;
    int err;
    if ((err = split(tree, path, &target, &parent)) != 0) return err;
    if (hmap_size(target->dirs) > 0) {
        hmap_insert(parent->dirs, target->name, target);
        return ENOTEMPTY;
    }
    tree_seq_free(target);
    return 0;
}

int tree_seq_move(TreeSeq* tree, const char* source_path, const char* target_path) {
    if (is_root_path(source_path)) return EBUSY;

    TreeSeq* target_parent;
    char* target_name;
    int err;
    if ((err = get_parent(tree, target_path, &target_parent, &target_name)) != 0)
        return (err == EBUSY ? EEXIST : err);

    TreeSeq* source;
    TreeSeq* source_parent;
    if ((err = split(tree, source_path, &source, &source_parent)) != 0) {
        free(target_name);
        return err;
    }

    if (strcmp(source_path, target_path) == 0) {
        hmap_insert(source_parent->dirs, source->name, source);
        free(target_name);
        return 0;
    }

    size_t source_path_len = strlen(source_path);
    if (strncmp(source_path, target_path, source_path_len) == 0) {
        hmap_insert(source_parent->dirs, source->name, source);
        free(target_name);
        return -1;
    }

    if (hmap_get(target_parent->dirs, target_name) != NULL) {
        hmap_insert(source_parent->dirs, source->name, source);
        free(target_name);
        return EEXIST;
    }

    free((char*) source->name);
    source->name = target_name;
    hmap_insert(target_parent->dirs, target_name, source);

    return 0;
}
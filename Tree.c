#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "Tree.h"
#include "err.h"
#include "path_utils.h"
#include "HashMap.h"
#include "safealloc.h"
#include "pthread.h"

const bool DEBUG = true;
void deb(const char* s) {if (DEBUG) fprintf(stderr, "%s\n", s);}

#define pthread_exec(expr) do { \
    int errno; \
    if ((errno = (expr)) != 0) syserr(errno, "Error in pthread"); \
} while (0)

#define tree_exec(expr) do { \
    int errno; \
    if ((errno = (expr)) != 0) return errno; \
} while (0)

struct Tree {
    const char* name;
    HashMap* dirs;
    Tree* parent;
    pthread_rwlock_t*  rwlock;
};

void tree_print_helper(Tree* tree, char* path, size_t path_len) {
    const char* key;
    void* kid;
    HashMapIterator it = hmap_iterator(tree->dirs);
    int count = 0;
    while (hmap_next(tree->dirs, &it, &key, &kid)) {
        count++;
        char* new_path = malloc(MAX_PATH_LENGTH  + 1);
        strncpy(new_path, path, path_len);
        size_t name_len = strlen(((Tree*) kid)->name);
        const char* name = ((Tree*) kid)->name;
        strncpy(new_path + path_len, name, name_len);
        new_path[path_len + name_len] = '/';
        new_path[path_len + name_len + 1] = '\0';
        tree_print_helper((Tree*) kid, new_path, path_len + name_len + 1);
    }

    if (count == 0) fprintf(stderr, "%s\n", path);
    free(path);
}

char* get_root_name() {
    char* name =safe_malloc(2);
    name[0] = '/';
    name[1] = '\0';
    return name;
}

void tree_print(Tree* tree) {
    char* path = get_root_name();
    tree_print_helper(tree, path, 1);
    fprintf(stderr, "\n");
}

Tree* tree_new_unnamed() {
    Tree* tree = safe_malloc(sizeof(Tree));
    tree->dirs = hmap_new();
    tree->parent = NULL;

    tree->rwlock = safe_malloc(sizeof(pthread_rwlock_t));
    pthread_exec(pthread_rwlock_init(tree->rwlock, NULL));
    return tree;
}

Tree* tree_new() {
    Tree* tree = tree_new_unnamed();
    tree->name = get_root_name();
    return tree;
}

Tree* tree_new_dir(const char* name, Tree* parent) {
    Tree* tree = tree_new_unnamed();
    tree->name = name;
    tree->parent = parent;
    return tree;
}

void tree_free(Tree* tree) {
    const char* key;
    void* kid;
    HashMapIterator it = hmap_iterator(tree->dirs);
    while (hmap_next(tree->dirs, &it, &key, &kid)) {
        hmap_remove(tree->dirs, ((Tree*) kid)->name);
        tree_free(kid);
    }
    free((char*) tree->name);
    free(tree->rwlock);
    hmap_free(tree->dirs);
    free(tree);
}

bool is_root_path(const char* path) {
    return (path[0] == '/' && path[1] == '\0');
}

void tree_unlock_to_root(Tree* tree) {
    while (tree) {
        pthread_exec(pthread_rwlock_unlock(tree->rwlock));
        tree = tree->parent;
    }
}

void tree_rdlock(Tree* tree) {
    pthread_exec(pthread_rwlock_rdlock(tree->rwlock));
}

void tree_rwlock(Tree* tree) {
    pthread_exec(pthread_rwlock_wrlock(tree->rwlock));
}

enum path_rdlock_scope {
    NONE = 0, INCLUDING = 1, EXCLUDING = 2
};

typedef enum path_rdlock_scope path_rdlock_scope;

Tree* tree_get_subdir(Tree* tree, const char* subpath,
                      path_rdlock_scope rdlock) {
    char component[MAX_FOLDER_NAME_LENGTH + 1];
    Tree* kid;
    while ((subpath = split_path(subpath, component))) {
        if (rdlock) tree_rdlock(tree);
        if (!(kid = hmap_get(tree->dirs, component))) {
            if (rdlock) tree_unlock_to_root(tree);
            return NULL;
        }
        tree = kid;
    }
    if (rdlock == INCLUDING) tree_rdlock(tree);
    return tree;
}

char* tree_list(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return NULL;
    Tree* subdir = tree_get_subdir(tree, path, INCLUDING);
    if (subdir == NULL) return NULL;
    char* s = make_map_contents_string(subdir->dirs);
    tree_unlock_to_root(subdir);
    return s;
}

int get_parent(Tree* tree, const char* path, Tree** parent, char** name,
               path_rdlock_scope rdlock) {
    if (!is_path_valid(path)) return EINVAL;
    char* component = safe_malloc(MAX_FOLDER_NAME_LENGTH + 1);
    char* parent_path = make_path_to_parent(path, component);
    if (parent_path == NULL) { // path is "/".
        free(component);
        return EBUSY;
    }
    *parent = tree_get_subdir(tree, parent_path, rdlock);
    free(parent_path);
    if (*parent == NULL) {
        free(component);
        return ENOENT;
    }
    *name = component;
    return 0;
}

int tree_create(Tree* tree, const char* path) {
    deb("CREATE");
    Tree* parent;
    char* name;
    if (is_root_path(path)) return EEXIST; // get_parent would return EBUSY.
    tree_exec(get_parent(tree, path, &parent, &name, EXCLUDING));
    tree_rwlock(parent);
    if (hmap_get(parent->dirs, name) != NULL) {
        free(name);
        tree_unlock_to_root(parent);
        return EEXIST;
    }
    Tree* new_dir = tree_new_dir(name, parent);
    hmap_insert(parent->dirs, name, new_dir);
    tree_unlock_to_root(parent);
    return 0;
}

int split(Tree* tree, const char* path, Tree** splitted, Tree** _parent, bool rdlock) {
    Tree* parent;
    char* name;
    tree_exec(get_parent(tree, path, &parent, &name, rdlock ? EXCLUDING : NONE));
    if (rdlock) tree_rwlock(parent);
    if (!(*splitted = hmap_get(parent->dirs, name))) {
        free(name);
        if (rdlock) tree_unlock_to_root(parent);
        return ENOENT;
    }
    hmap_remove(parent->dirs, name);
    free(name);
    *_parent = parent;
    return 0;
}

int tree_remove(Tree* tree, const char* path) {
    deb("REMOVE");
    Tree* target;
    Tree* parent;
    tree_exec(split(tree, path, &target, &parent, true));
    if (hmap_size(target->dirs) > 0) {
        hmap_insert(parent->dirs, target->name, target);
        tree_unlock_to_root(parent);
        return ENOTEMPTY;
    }
    tree_free(target);
    tree_unlock_to_root(parent);
    return 0;
}

bool split_paths_common(const char** source, const char** target,
                        char* component1, char* component2) {
    bool success = (*source = split_path(*source, component1));
    success = (*target = split_path(*target, component2)) && success;
    success = (strcmp(component1, component2) != 0) && success;
    return success;
}

int tree_get_lca(Tree* tree, const char* source, const char* target, Tree** lca) {
    if (!is_path_valid(source) || !is_path_valid(target)) return EINVAL;
    char component1[MAX_FOLDER_NAME_LENGTH + 1];
    char component2[MAX_FOLDER_NAME_LENGTH + 1];
    Tree* parent = NULL;
    while (split_paths_common(&source, &target, component1, component2)) {
        tree_rdlock(tree);
        parent = tree;
        if (!(tree = hmap_get(tree->dirs, component1))) {
            tree_unlock_to_root(parent);
            return ENOENT;
        }
    }
    if (source == NULL && target == NULL) {
        if (parent) tree_unlock_to_root(parent);
        return 0;
    }
    if (source == NULL) {
        if (parent) tree_unlock_to_root(parent);
        return -1;
    }
    if (target == NULL) {
        if (parent) tree_unlock_to_root(parent);
        return EEXIST;
    }

    deb(tree->name);
    tree_rwlock(tree);
    deb(tree->name);
    *lca = tree;
    return 0;
}

int tree_move(Tree* tree, const char* source_path, const char* target_path) {
    deb("MOVE");
    if (is_root_path(source_path)) return EBUSY;

    Tree* lca;
    deb("LOL");
    tree_exec(tree_get_lca(tree, source_path, target_path, &lca));
    deb(lca->name);

    Tree* target_parent;
    char* target_name;
    int err;
    if ((err = get_parent(tree, target_path, &target_parent, &target_name, false)) != 0) {
        tree_unlock_to_root(lca);
        return (err == EBUSY ? EEXIST : err);
    }

    Tree* source;
    Tree* source_parent;
    if ((err = split(tree, source_path, &source, &source_parent, false)) != 0) {
        free(target_name);
        tree_unlock_to_root(lca);
        return err;
    }

    if (hmap_get(target_parent->dirs, target_name) != NULL) {
        hmap_insert(source_parent->dirs, source->name, source);
        free(target_name);
        tree_unlock_to_root(lca);
        return EEXIST;
    }

    free((char*) source->name);
    source->name = target_name;
    hmap_insert(target_parent->dirs, target_name, source);
    source->parent = target_parent;
    tree_unlock_to_root(lca);
    return 0;
}
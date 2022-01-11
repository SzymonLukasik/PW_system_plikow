#pragma once

typedef struct TreeSeq TreeSeq;

void tree_seq_print(TreeSeq*);

TreeSeq* tree_seq_new(Tree*);

void tree_seq_free(TreeSeq*);

char* tree_seq_list(TreeSeq* tree, const char* path);

int tree_seq_create(TreeSeq* tree, const char* path);

int tree_seq_remove(TreeSeq* tree, const char* path);

int tree_seq_move(TreeSeq* tree, const char* source, const char* target);

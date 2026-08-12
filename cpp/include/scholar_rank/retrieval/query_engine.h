#ifndef QUERY_ENGINE_H
#define QUERY_ENGINE_H

#include <limits>

// A SafeMMAP class here for deep-pointer movement.

const unsigned long long MAX_DOC_ID = std::numeric_limits<unsigned long long>::max();

void find_pivot();

void move_posting();

void move_posting_shallow();

void full_evaluate();

void get_new_candidate();

#endif
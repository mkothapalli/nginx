
/*
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_URI_HASH_H_INCLUDED_
#define _NGX_URI_HASH_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <stdbool.h>

typedef struct ngx_lru_link_node_s ngx_lru_link_node;

struct ngx_lru_link_node_s {
  ngx_lru_link_node * prev;
  ngx_lru_link_node * next;
} ;

typedef struct  {
  ngx_lru_link_node * head;
  ngx_lru_link_node * tail;
} ngx_lru_link_list;

typedef struct ngx_uri_entry_s  ngx_uri_entry;

struct ngx_uri_entry_s {
  u_char uri[257];
  ngx_uri_entry   * next;
  ngx_uint_t        count;
  ngx_lru_link_node lru;
} ;

typedef struct {
  ngx_uri_entry **buckets;
  ngx_uint_t      size;  
} ngx_uri_hash_table;

typedef struct {
  ngx_uri_hash_table * hash_table;
  ngx_lru_link_list    lru_list;
  ngx_uint_t           lru_list_entries;
  ngx_uint_t           lru_list_max_entries;
  ngx_pool_t         * pool;
  ngx_log_t          * log;
} ngx_uri_table;

bool ngx_uri_table_init(ngx_pool_t * pool, ngx_log_t * log, ngx_uri_table * uri_table);
bool ngx_uri_table_add(ngx_uri_table * uri_table, const ngx_str_t * uri);
void ngx_uri_table_report(ngx_uri_table * uri_table, ngx_str_t * report);
void ngx_uri_table_cleanup(ngx_uri_table * uri_table);

// max-heap impl to find k most popular urls in sorted order
typedef struct ngx_max_heap {
  ngx_uint_t size;
  ngx_uint_t capacity;
  ngx_uri_entry ** elements;
} ngx_max_heap;

#endif


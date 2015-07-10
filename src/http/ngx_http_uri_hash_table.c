#include "ngx_http_uri_hash_table.h"
#include <math.h>

#define TOPN 100
#define INITIAL_SIZE 1024
// predefine functions
void
ngx_uri_table_lru_list_purge(ngx_uri_table * uri_table, bool force_purge);
void
ngx_uri_table_lru_list_add(ngx_uri_table * uri_table, ngx_uri_entry * new_entry);
void
ngx_uri_table_lru_list_delete(ngx_uri_table * uri_table, ngx_uri_entry * old_entry);
void
ngx_uri_table_lru_list_walk(ngx_uri_table * uri_table, ngx_str_t * report);

// heap functions
ngx_max_heap *
ngx_max_heap_init(ngx_uint_t heap_size, ngx_log_t * log);
void
ngx_max_heap_build(ngx_max_heap * max_heap, ngx_uri_entry * uri_entry);
bool
ngx_max_heap_add(ngx_max_heap * max_heap, ngx_uri_entry * uri_entry);
ngx_uri_entry *
ngx_max_heap_delete(ngx_max_heap * max_heap);
void
ngx_max_heap_heapify(ngx_max_heap * max_heap, ngx_uint_t i);
void
ngx_max_heap_swap(ngx_uri_entry **n1, ngx_uri_entry **n2);
ngx_uint_t
ngx_max_heap_size(ngx_max_heap * max_heap);
void
ngx_max_heap_reset(ngx_max_heap * max_heap, ngx_uint_t heap_size);
ngx_uint_t
ngx_max_heap_get_topn(ngx_max_heap * max_heap, u_char * report);
ngx_uri_entry*
ngx_max_heap_root_element(ngx_max_heap * max_heap);

// predeclare global heap
ngx_max_heap * max_heap = NULL;

/* Hash function from Chris Torek. */
ngx_uint_t
hash4(const void *data, ngx_uint_t size)
{
  const char *key = (const char *)data;
  size_t loop;
  ngx_uint_t h;
  size_t len;

#define HASH4a   h = (h << 5) - h + *key++;
#define HASH4b   h = (h << 5) + h + *key++;
#define HASH4 HASH4b

  h = 0;
  len = strlen(key);
  loop = len >> 3;
  switch (len & (8 - 1))
  {
    case 0:
      break;
    case 7:
      HASH4;
      /* FALLTHROUGH */
    case 6:
      HASH4;
      /* FALLTHROUGH */
    case 5:
      HASH4;
      /* FALLTHROUGH */
    case 4:
      HASH4;
      /* FALLTHROUGH */
    case 3:
      HASH4;
      /* FALLTHROUGH */
    case 2:
      HASH4;
      /* FALLTHROUGH */
    case 1:
      HASH4;
  }
  while (loop--)
  {
    HASH4;
    HASH4;
    HASH4;
    HASH4;
    HASH4;
    HASH4;
    HASH4;
    HASH4;
  }
  return h % size;
}

static const ngx_int_t hash_primes[] =
{
  103,
  229,
  467,
  977,
  1979,
  4019,
  6037,
  7951,
  12149,
  16231,
  33493,
  65357
};

ngx_int_t
hash_prime(ngx_int_t n)
{
  ngx_int_t I = sizeof(hash_primes) / sizeof(int);
  ngx_int_t i;
  ngx_int_t best_prime = hash_primes[0];
  double min = fabs(log((double) n) - log((double) hash_primes[0]));
  double d;
  for (i = 0; i < I; i++)
  {
    d = fabs(log((double) n) - log((double) hash_primes[i]));
    if (d > min)
      continue;
    min = d;
    best_prime = hash_primes[i];
  }
  return best_prime;
}

bool
ngx_uri_table_init(ngx_pool_t * pool, ngx_log_t *log, ngx_uri_table * uri_table)
{
  if (uri_table->hash_table != NULL) {
    return false;
  }
  
  uri_table->hash_table = ngx_alloc(sizeof(ngx_uri_hash_table), log);
  if (uri_table->hash_table == NULL)
    return false;

  // calculate hash table size given the 2MB memory constraint
  size_t x = sizeof(ngx_uri_entry) + 2 * sizeof(ngx_uri_entry*);
  ngx_int_t num_hash_entries = (2 * 1024 * 1024)/x;
  ngx_int_t hash_size = hash_prime(2 * num_hash_entries);

  uri_table->hash_table->buckets = ngx_alloc(hash_size * sizeof(ngx_uri_entry*), log);
  if (uri_table->hash_table->buckets == NULL)
    return false;

  // initialize
  uri_table->hash_table->size = hash_size; 
  uri_table->lru_list_entries = 0;
  uri_table->lru_list_max_entries = num_hash_entries;
  uri_table->lru_list.head   = NULL;
  uri_table->lru_list.tail   = NULL;
  uri_table->pool            = pool;
  uri_table->log             = log;
  
  return true;
}

ngx_uri_entry *
ngx_uri_table_lookup(ngx_uri_table * uri_table, const u_char * uri)
{
  if (uri == NULL)
    return NULL;
  
  ngx_uint_t v = hash4(uri, uri_table->hash_table->size);
    
  ngx_uri_entry * walker;

  for (walker = uri_table->hash_table->buckets[v]; walker != NULL; walker = walker->next)
  {
    if (ngx_strcmp(uri, walker->uri) == 0) {
      return walker;
    }
  }
  return NULL;
}

void
ngx_uri_table_join(ngx_uri_table * uri_table, ngx_uri_entry * new_entry)
{
    ngx_uint_t v = hash4(new_entry->uri, uri_table->hash_table->size);
    new_entry->next = uri_table->hash_table->buckets[v];
    uri_table->hash_table->buckets[v] = new_entry;
}

// refresh the lru list such that the entry is now at the head of the list
void
ngx_uri_table_update(ngx_uri_table * uri_table, ngx_uri_entry * old_entry)
{
  ngx_uri_table_lru_list_delete(uri_table, old_entry);
  ngx_uri_table_lru_list_add(uri_table, old_entry);
}

bool
ngx_uri_table_add(ngx_uri_table * uri_table, const ngx_str_t * uri)
{
  if (uri->len <= 0 || uri->len > 256) {
    return false;
  }

  // normalize uri
  u_char * my_uri;
  my_uri = ngx_palloc(uri_table->pool, uri->len+1);
  if (my_uri == NULL) {
    return false;
  }
  ngx_strlow(my_uri, uri->data, uri->len);
  my_uri[uri->len] = '\0';

  ngx_uri_entry * entry = ngx_uri_table_lookup(uri_table, my_uri);
  if (entry) {
    entry->count++;
    ngx_uri_table_update(uri_table, entry);
    return true;
  } 

  // free up memory if needed
  ngx_uri_table_lru_list_purge(uri_table, false);
  ngx_uri_entry * new_entry = ngx_alloc(sizeof(ngx_uri_entry), uri_table->log);
  if (new_entry == NULL)
    return false; 

  // copy uri including the null terminating character
  ngx_memcpy(new_entry->uri, my_uri, uri->len+1);
  new_entry->count = 1;
  ngx_uri_table_join(uri_table, new_entry);
  ngx_uri_table_lru_list_add(uri_table, new_entry);
  return true;
}

void
ngx_uri_table_delete(ngx_uri_table * uri_table, ngx_uri_entry * cur_entry)
{
    ngx_uint_t v = hash4(cur_entry->uri, uri_table->hash_table->size);

    if (uri_table->hash_table->buckets[v] == cur_entry)
        uri_table->hash_table->buckets[v] = cur_entry->next;
    else
    {
      ngx_uri_entry * walker;
      for (walker=uri_table->hash_table->buckets[v];walker;walker=walker->next)
      {
          if (walker->next == cur_entry) {
            walker->next = cur_entry->next;
            break;
          }
      }
    }
}

void
ngx_uri_table_lru_list_delete(ngx_uri_table * uri_table, ngx_uri_entry * old_entry)
{
  if (old_entry->lru.next)
      old_entry->lru.next->prev = old_entry->lru.prev;
  if (old_entry->lru.prev)
      old_entry->lru.prev->next = old_entry->lru.next;
  if (&old_entry->lru == uri_table->lru_list.head)
      uri_table->lru_list.head = old_entry->lru.next;
  if (&old_entry->lru == uri_table->lru_list.tail)
      uri_table->lru_list.tail = old_entry->lru.prev;

  old_entry->lru.next = old_entry->lru.prev = NULL;
  --uri_table->lru_list_entries;
}

void
ngx_uri_table_lru_list_add(ngx_uri_table * uri_table, ngx_uri_entry * new_entry)
{
  new_entry->lru.prev = NULL;
  new_entry->lru.next = uri_table->lru_list.head;
  if (uri_table->lru_list.head) {
    uri_table->lru_list.head->prev = &new_entry->lru;
  }
  uri_table->lru_list.head = &new_entry->lru;
  if (uri_table->lru_list.tail == NULL) {
    uri_table->lru_list.tail = &new_entry->lru;
  }    
  ++uri_table->lru_list_entries;
}

#define DUMMY_ADDRESS ((char *)0x1000)
#define STRUCT_BASE_OFFSET(s,n) (((char *) &(((s *) DUMMY_ADDRESS)->n)) - ((char *) DUMMY_ADDRESS))

#define LINK_TO_STRUCT(d,n,s)                          \
  ( (s *) (((char *) (d)) - STRUCT_BASE_OFFSET(s,n)) )

void
ngx_uri_table_lru_list_purge(ngx_uri_table * uri_table, bool force_purge)
{
    ngx_lru_link_node * m;
    ngx_lru_link_node * prev;
    ngx_uri_entry *     entry;
    for (m = uri_table->lru_list.tail; m; m = prev) {
        if ((!force_purge) && (uri_table->lru_list_entries < uri_table->lru_list_max_entries)) break;
        prev = m->prev;
        entry = LINK_TO_STRUCT(m, lru, ngx_uri_entry);
        // unlink lru
        ngx_lru_link_node * cur_node = &entry->lru;
        if (cur_node->next)
            cur_node->next->prev = cur_node->prev;
        if (cur_node->prev)
            cur_node->prev->next = cur_node->next;
        if (cur_node == uri_table->lru_list.head)
            uri_table->lru_list.head = cur_node->next;
        if (cur_node == uri_table->lru_list.tail)
            uri_table->lru_list.tail = cur_node->prev;
        cur_node->next = cur_node->prev = NULL;
   
        // now unlink from the hash table
        ngx_uri_entry * cur_entry = ngx_uri_table_lookup(uri_table, (const u_char *) entry->uri);
        if (cur_entry) {
            ngx_uri_table_delete(uri_table, cur_entry);
        }
        // release the memory
        ngx_pfree(uri_table->pool, cur_entry);
    }
}

void ngx_uri_table_report(ngx_uri_table * uri_table, ngx_str_t * report)
{
  //ngx_uri_table_lru_list_walk(uri_table, report);

  ngx_lru_link_node * m;
  ngx_uri_entry *     entry;

  if (max_heap == NULL) {
    max_heap = ngx_max_heap_init(INITIAL_SIZE, uri_table->log);
  } else {
    ngx_max_heap_reset(max_heap, uri_table->lru_list_entries);
  }

  // build the heap
  for (m = uri_table->lru_list.head; m; m = m->next) {
    entry = LINK_TO_STRUCT(m, lru, ngx_uri_entry);
    ngx_max_heap_add(max_heap, entry);
  }

  u_char            * temp_string;

  temp_string = ngx_palloc(uri_table->pool, 270 * ngx_max_heap_size(max_heap));
  if (temp_string == NULL) {
    report->len = 0;
    return;
  }

  ngx_uint_t len = ngx_max_heap_get_topn(max_heap, temp_string);

  report->len = len;
  report->data = temp_string;
  return;
}

void
ngx_uri_table_lru_list_walk(ngx_uri_table * uri_table, ngx_str_t * report)
{
  ngx_lru_link_node * m;
  ngx_uri_entry *     entry;
  u_char            * temp_string;
  ngx_uint_t          len = 0;
  ngx_uint_t          total_len = 0;
  temp_string = ngx_palloc(uri_table->pool, 4096*sizeof(u_char));
  u_char * orig_addr = temp_string;

  for (m = uri_table->lru_list.head; m; m = m->next) {
      entry = LINK_TO_STRUCT(m, lru, ngx_uri_entry);
      len = strlen((const char*)entry->uri);
      ngx_memcpy(temp_string, entry->uri, len);
      temp_string[len++] = ' ';
      temp_string = ngx_sprintf(temp_string+len, "%d", entry->count);
      *temp_string = '\n';
      temp_string++;
      total_len += len+1;
      // quick hack to finish up faster
      if (entry->count <10)
        total_len += 1;
      else if (entry->count < 100)
        total_len += 2;
      else if (entry->count < 1000)
        total_len += 2;
  }
  report->len = total_len;
  report->data = orig_addr;
  return;
}

void
ngx_uri_table_cleanup(ngx_uri_table * uri_table)
{
    if (uri_table->hash_table == NULL) {
      return;
    }
    bool force_purge = true;
    ngx_uri_table_lru_list_purge(uri_table, force_purge);
}

// Max Heap Implementation
#define LCHILD(x) 2 * x + 1
#define RCHILD(x) 2 * x + 2
#define PARENT(x) x / 2

ngx_max_heap *
ngx_max_heap_init(ngx_uint_t heap_size, ngx_log_t * log)
{
  ngx_max_heap * max_heap = ngx_alloc(1 * sizeof(ngx_max_heap), log);
  if (max_heap == NULL)
    return NULL;
  max_heap->elements = ngx_alloc(heap_size * sizeof(ngx_uri_entry*), log);
  if (max_heap->elements == NULL)
    return NULL;
  max_heap->capacity = heap_size;
  max_heap->size = 0; // no elements yet in the heap
  return max_heap;
}

ngx_uri_entry*
ngx_max_heap_root_element(ngx_max_heap * max_heap)
{
  if (max_heap && (max_heap->size > 0) ) {
    return max_heap->elements[0];
  }
  // this code path never executes, just keeping the compiler happy
  return 0;
}

bool
ngx_max_heap_add(ngx_max_heap * max_heap, ngx_uri_entry * uri_entry)
{
  ngx_uint_t i = (max_heap->size)++;
  while(i && uri_entry->count > max_heap->elements[PARENT(i)]->count)
  {
    max_heap->elements[i] = max_heap->elements[PARENT(i)];
    i = PARENT(i);
  }
  max_heap->elements[i] = uri_entry;
  return true;
}

ngx_uri_entry *
ngx_max_heap_delete(ngx_max_heap * max_heap)
{
  ngx_uri_entry * temp = NULL;
  if(max_heap->size > 0) {
    temp = max_heap->elements[0];
    max_heap->elements[0] = max_heap->elements[--(max_heap->size)];
    ngx_max_heap_heapify(max_heap, 0);
  }
  return temp;
}

void
ngx_max_heap_heapify(ngx_max_heap * max_heap, ngx_uint_t i)
{
  ngx_uint_t largest = (LCHILD(i) < max_heap->size &&
    max_heap->elements[LCHILD(i)]->count > max_heap->elements[i]->count) ? LCHILD(i) : i;
  if(RCHILD(i) < max_heap->size
      && max_heap->elements[RCHILD(i)]->count > max_heap->elements[largest]->count) {
    largest = RCHILD(i) ;
  }

  if(largest != i) {
    ngx_max_heap_swap(&(max_heap->elements[i]), &(max_heap->elements[largest]));
    ngx_max_heap_heapify(max_heap, largest);
  }
}

void
ngx_max_heap_swap(ngx_uri_entry **n1, ngx_uri_entry **n2)
{
    ngx_uri_entry * temp = *n1 ;
    *n1 = *n2 ;
    *n2 = temp ;
}

ngx_uint_t
ngx_max_heap_size(ngx_max_heap * max_heap)
{
  if (max_heap) {
    return max_heap->size;
  } else {
    return 0;
  }
}

void
ngx_max_heap_reset(ngx_max_heap * max_heap, ngx_uint_t heap_size)
{
  if (max_heap) {
    max_heap->size = 0;
    if (heap_size > max_heap->capacity) {
      // hopefully this doesn't happen too frequently, else we could look
      // at alternate ideas like a persistent max heap of TOPN elements
      // that we update dynamically
      // or we might use memory from ngx_pool instead of going to the kernel
      max_heap = realloc((void *)max_heap, max_heap->capacity * 2 * sizeof(ngx_uri_entry*));
      max_heap->capacity *= 2;
    }
  }
}

ngx_uint_t
ngx_max_heap_get_topn(ngx_max_heap * max_heap, u_char * report)
{
  ngx_uint_t total_len = 0;
  ngx_uint_t count = 0;
  while( max_heap->size > 0 && count < TOPN) {
    count++;
    ngx_uri_entry * entry = ngx_max_heap_delete(max_heap);
    ngx_uint_t len = strlen((const char *)entry->uri);
    ngx_memcpy(report, entry->uri, len);
    report[len++] = ' ';
    report = ngx_sprintf(report + len, "%d%N", entry->count);
    // quick hack to finish up faster
    if (entry->count <10)
      total_len += len+2;
    else if (entry->count < 100)
      total_len += len+3;
    else if (entry->count < 1000)
      total_len += len+4;
  }
  return total_len;
}

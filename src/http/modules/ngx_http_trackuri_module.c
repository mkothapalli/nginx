
/*
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_uri_hash_table.h>

// trackuri directives
typedef struct {
    ngx_flag_t    track_uri;
    ngx_flag_t    return_uri_stats;
    ngx_uri_table uri_table;
} ngx_http_trackuri_loc_conf_t;

// predefine functions
static char *
ngx_http_trackuri_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
//static void
//ngx_http_trackuri_cleanup(void *data);
static ngx_int_t
ngx_http_trackuri_handler(ngx_http_request_t *r);
static char *
ngx_http_trackuri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void *
ngx_http_trackuri_create_loc_conf(ngx_conf_t *cf);
static char *
ngx_http_return_uristats(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

// trackuri directives
static ngx_command_t  ngx_http_trackuri_commands[] = {

    { ngx_string("popular_uri_track"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_trackuri,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("popular_uri_stats"),
      NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_http_return_uristats,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};

static ngx_http_module_t  ngx_http_trackuri_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_trackuri_create_loc_conf,     /* create location configuration */
    ngx_http_trackuri_merge_loc_conf       /* merge location configuration */
};

ngx_module_t  ngx_http_trackuri_module = {
    NGX_MODULE_V1,
    &ngx_http_trackuri_module_ctx,         /* module context */
    ngx_http_trackuri_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static char *
ngx_http_trackuri_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_trackuri_loc_conf_t *prev = parent;
    ngx_http_trackuri_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->track_uri, prev->track_uri, 0);
    ngx_conf_merge_value(conf->return_uri_stats, prev->return_uri_stats, 0);

    return NGX_CONF_OK;
}

static ngx_str_t  ngx_http_text_type = ngx_string("text/plain");
//static ngx_str_t  popular_uri_stats = ngx_string("/images/1.jpg 100\n/images/2.jpg 45\n/test.html 200\n");

static ngx_int_t
ngx_http_trackuri_handler(ngx_http_request_t *r)
{
    ngx_http_trackuri_loc_conf_t  *flcf;
    flcf = ngx_http_get_module_loc_conf(r, ngx_http_trackuri_module);
    if (flcf->track_uri != 1)
        return NGX_HTTP_NOT_ALLOWED;

    if (!ngx_uri_table_add(&flcf->uri_table, &r->uri))
        return NGX_HTTP_INTERNAL_SERVER_ERROR;

    // add top-n stats to response body.
    if ((r->method & NGX_HTTP_GET) && flcf->return_uri_stats == 1) {
        ngx_str_t report;
        ngx_uri_table_report(&flcf->uri_table, &report);
        ngx_http_complex_value_t  cv;
        ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));
        cv.value.len  = report.len;
        cv.value.data = report.data;
        r->headers_out.last_modified_time = 23349600;
        return ngx_http_send_response(r, NGX_HTTP_OK, &ngx_http_text_type, &cv);
    }

    return NGX_HTTP_CLOSE;
}

static char *
ngx_http_trackuri(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_trackuri_loc_conf_t *flcf = conf;

    if (flcf->track_uri != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    ngx_str_t        *value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
        flcf->track_uri = 1;
    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
        flcf->track_uri = 0;
    } else {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                         "invalid value \"%s\" in \"%s\" directive, "
                         "it must be \"on\" or \"off\"",
                         value[1].data, cmd->name.data);
        return NGX_CONF_ERROR;
    }

    if (flcf->track_uri == 1)
    {
        ngx_http_core_loc_conf_t   *clcf;
        clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
        clcf->handler = ngx_http_trackuri_handler;

        // initialize uri_table to start tracking popular uris
        if (!ngx_uri_table_init(cf->pool, cf->log, &flcf->uri_table))
            return NGX_CONF_ERROR;
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "Initialized hash table of size: \"%d\"",
                           flcf->uri_table.hash_table->size);
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_return_uristats(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_trackuri_loc_conf_t *flcf = conf;

    if (flcf->return_uri_stats != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    ngx_str_t        *value = cf->args->elts;

    if (ngx_strcasecmp(value[1].data, (u_char *) "on") == 0) {
      flcf->return_uri_stats = 1;
    } else if (ngx_strcasecmp(value[1].data, (u_char *) "off") == 0) {
      flcf->return_uri_stats = 0;
    } else {
      ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                         "invalid value \"%s\" in \"%s\" directive, "
                         "it must be \"on\" or \"off\"",
                         value[1].data, cmd->name.data);
      return NGX_CONF_ERROR;
    }

    if (flcf->return_uri_stats)
    {
      if (!flcf->track_uri)
      {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%s\" directive is set, but popular_uri_track directive is not set",
                           cmd->name.data);
        return NGX_CONF_ERROR;
      }
    }

    return NGX_CONF_OK;
}

static void *
ngx_http_trackuri_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_trackuri_loc_conf_t  *conf;
//    ngx_pool_cleanup_t            *cln;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_trackuri_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->track_uri        = NGX_CONF_UNSET;
    conf->return_uri_stats = NGX_CONF_UNSET;

    /*cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NULL;
    }

    cln->handler = ngx_http_trackuri_cleanup;
    cln->data = conf;*/

    return conf;
}

/*static void
ngx_http_trackuri_cleanup(void *data)
{
    ngx_http_trackuri_loc_conf_t  *conf = data;

    if (conf->uri_table.hash_table) {
        ngx_uri_table_cleanup(&conf->uri_table);
    }
}*/


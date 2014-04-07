/*
 * author mojidong
 * */
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {
	ngx_flag_t enable;
	ngx_uint_t begin;
	ngx_uint_t end;
	ngx_hash_t    types;
	ngx_array_t  *types_keys;
	ngx_str_t url;
	ngx_str_t new;
} ngx_http_img_url_filter_srv_conf_t;

typedef enum{
	img_url_filter_state_text = 0,
	img_url_filter_state_tag,
	img_url_filter_state_tag_img,
	img_url_filter_state_tag_img_end
}	ngxhttp_img_url_filter_state_e;

typedef struct{
	unsigned char state;
}	ngx_http_img_url_filter_ctx_t;

static	ngx_int_t	ngx_http_img_url_filter_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_img_url_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_img_url_body_filter(ngx_http_request_t *r,ngx_chain_t *in);
static void *ngx_http_img_url_filter_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_img_url_filter_merge_srv_conf(ngx_conf_t *cf,void *parent,void *child);

static void	ngx_http_filter_url(ngx_http_request_t *r,ngx_buf_t *buf,ngx_http_img_url_filter_srv_conf_t *ufc,ngx_http_img_url_filter_ctx_t *ctx);

static ngx_command_t ngx_http_img_url_filter_commands[]={
		{
				ngx_string("img_url_filter"),
				NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
				ngx_conf_set_flag_slot,
				NGX_HTTP_SRV_CONF_OFFSET,
				offsetof(ngx_http_img_url_filter_srv_conf_t,enable),
				NULL
		},
		{
				ngx_string("img_url_filter_begin"),
				NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_num_slot,
				NGX_HTTP_SRV_CONF_OFFSET,
				offsetof(ngx_http_img_url_filter_srv_conf_t,begin),
				NULL
		},
		{
				ngx_string("img_url_filter_end"),
				NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_num_slot,
				NGX_HTTP_SRV_CONF_OFFSET,
				offsetof(ngx_http_img_url_filter_srv_conf_t,end),
				NULL
		},
		{
				ngx_string("img_url_filter_types"),
				NGX_HTTP_SRV_CONF|NGX_CONF_1MORE,
				ngx_http_types_slot,
				NGX_HTTP_SRV_CONF_OFFSET,
				offsetof(ngx_http_img_url_filter_srv_conf_t,types_keys),
				&ngx_http_html_default_types[0]
		},
		{
				ngx_string("img_url_filter_url"),
				NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_str_slot,
				NGX_HTTP_SRV_CONF_OFFSET,
				offsetof(ngx_http_img_url_filter_srv_conf_t,url),
				NULL
		},
		{
				ngx_string("img_url_filter_new"),
				NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
				ngx_conf_set_str_slot,
				NGX_HTTP_SRV_CONF_OFFSET,
				offsetof(ngx_http_img_url_filter_srv_conf_t,new),
				NULL
		},
		ngx_null_command
};

static ngx_http_module_t ngx_http_img_url_filter_module_ctx={
		NULL,
		ngx_http_img_url_filter_init,

		NULL,
		NULL,

		ngx_http_img_url_filter_create_srv_conf,
		ngx_http_img_url_filter_merge_srv_conf,

		NULL,
		NULL
};

ngx_module_t ngx_http_img_url_filter_module={
		NGX_MODULE_V1,
		&ngx_http_img_url_filter_module_ctx,
		ngx_http_img_url_filter_commands,
		NGX_HTTP_MODULE,
		NULL,
		NULL,
		NULL,
		NULL,

		NULL,
		NULL,
		NULL,
		NGX_MODULE_V1_PADDING
};

static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;

static ngx_int_t ngx_http_img_url_header_filter(ngx_http_request_t *r){
	ngx_http_img_url_filter_srv_conf_t *ufc;
	ngx_http_img_url_filter_ctx_t *ctx;
	ufc=ngx_http_get_module_srv_conf(r,ngx_http_img_url_filter_module);
	if(ufc==NULL)
		return ngx_http_next_header_filter(r);
	if(!ufc->enable || (r->headers_out.status!=NGX_HTTP_OK && r->headers_out.status!=NGX_HTTP_FORBIDDEN && r->headers_out.status!=NGX_HTTP_NOT_FOUND)
			|| r->header_only
			|| r->headers_out.content_type.len==0
			|| (r->headers_out.content_encoding && r->headers_out.content_encoding->value.len) || !ngx_http_test_content_type(r, &ufc->types)){
			return ngx_http_next_header_filter(r);
	}
	ctx=ngx_palloc(r->pool,sizeof(ngx_http_img_url_filter_ctx_t));
	if(ctx==NULL)
			return NGX_ERROR;
	ctx->state=img_url_filter_state_text;
	ngx_http_set_ctx(r,ctx,ngx_http_img_url_filter_module);
	ngx_http_clear_content_length(r);
	ngx_http_clear_accept_ranges(r);
	r->main_filter_need_in_memory=1;
	return ngx_http_next_header_filter(r);
}

static ngx_int_t ngx_http_img_url_body_filter(ngx_http_request_t *r,ngx_chain_t *in){
	ngx_chain_t *cl;
	ngx_http_img_url_filter_srv_conf_t *ufc;
	ngx_http_img_url_filter_ctx_t *ctx;
	ufc=ngx_http_get_module_srv_conf(r,ngx_http_img_url_filter_module);
	ctx=ngx_http_get_module_ctx(r,ngx_http_img_url_filter_module);
	if(ctx==NULL)
		return ngx_http_next_body_filter(r,in);
	if (ngx_http_test_content_type(r, &ufc->types)) {
		for(cl=in;cl;cl=cl->next){
			ngx_http_filter_url(r,cl->buf,ufc,ctx);
		}
	}
	return ngx_http_next_body_filter(r,in);
}

static void	ngx_http_filter_url(ngx_http_request_t *r,ngx_buf_t *buf,ngx_http_img_url_filter_srv_conf_t *ufc,ngx_http_img_url_filter_ctx_t *ctx){
	u_char *reader;
	u_char *writer;
	u_char *new;
	u_char *img_tag;
	u_char *img_tag_end;
	size_t count=0;
	size_t offset;
	new=ngx_palloc(r->pool,(ufc->url.len+1)*sizeof(u_char));
	ngx_memzero(new,sizeof(new));
	for(reader=buf->pos;reader<buf->last;reader++){
		switch(ctx->state){
			case img_url_filter_state_text:
				if(*reader=='<'){
					img_tag=reader;
					ctx->state=img_url_filter_state_tag;
				}else{
					ctx->state=img_url_filter_state_text;
				}
				break;
			case img_url_filter_state_tag:
				if(ngx_strncasecmp(reader,(char *)"img ",ngx_strlen("img ")-1)==0){
					ctx->state=img_url_filter_state_tag_img;
				}else{
					ctx->state=img_url_filter_state_text;
				}
				break;
			case img_url_filter_state_tag_img:
				if(*reader=='>'){
					img_tag_end=reader;
					ctx->state=img_url_filter_state_tag_img_end;
				}
				break;
			case img_url_filter_state_tag_img_end:
				while(img_tag && img_tag<img_tag_end){
					img_tag=ngx_strnstr(img_tag,ufc->url.data,img_tag_end-img_tag);
					if(img_tag){
						reader=img_tag;
						ngx_sprintf(new,ufc->new.data,count%(ufc->end+1)+ufc->begin);
						ngx_memcpy(img_tag,new,ngx_strlen(new));
						img_tag=img_tag+ngx_strlen(new);
						ctx->state=img_url_filter_state_text;
						count++;
						/*
						 * 	如果new比url短则buf需移动补齐
						 */
						offset=ufc->url.len-ngx_strlen(new);
						if(offset){
							for(writer=img_tag;writer < buf->last;writer++){
								*writer=*(writer+offset);
							}
							buf->last=--writer;
						}
					}
				}
				ctx->state=img_url_filter_state_text;
				continue;
			default:
				break;
		}
	}
}


static ngx_int_t	ngx_http_img_url_filter_init(ngx_conf_t *cf){
	ngx_http_next_header_filter=ngx_http_top_header_filter;
	ngx_http_top_header_filter=ngx_http_img_url_header_filter;

	ngx_http_next_body_filter=ngx_http_top_body_filter;
	ngx_http_top_body_filter=ngx_http_img_url_body_filter;
	return NGX_OK;
}

static void *ngx_http_img_url_filter_create_srv_conf(ngx_conf_t *cf){
	ngx_http_img_url_filter_srv_conf_t *ufc;
	ufc=ngx_palloc(cf->pool,sizeof(ngx_http_img_url_filter_srv_conf_t));
	if(ufc==NULL)
			return NULL;
	ufc->enable=NGX_CONF_UNSET;
	ufc->begin=NGX_CONF_UNSET_UINT;
	ufc->end=NGX_CONF_UNSET_UINT;
	return ufc;
}

static char *ngx_http_img_url_filter_merge_srv_conf(ngx_conf_t *cf,void *parent,void *child){
	ngx_http_img_url_filter_srv_conf_t *p=parent;
	ngx_http_img_url_filter_srv_conf_t *c=child;
	ngx_conf_merge_value(c->enable,p->enable,0);
	ngx_conf_merge_uint_value(c->begin,p->begin,0);
	ngx_conf_merge_uint_value(c->end,p->end,0);
	ngx_conf_merge_str_value(c->url,p->url,"");
	ngx_conf_merge_str_value(c->new,p->new,"");
	if (ngx_http_merge_types(cf, &c->types_keys, &c->types,&p->types_keys, &p->types,ngx_http_html_default_types)!= NGX_OK){
			return NGX_CONF_ERROR;
	}
	return NGX_CONF_OK;
}



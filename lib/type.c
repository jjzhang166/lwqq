/**
 * @file   type.c
 * @author mathslinux <riegamaths@gmail.com>
 * @date   Sun May 20 23:01:57 2012
 * 
 * @brief  Linux WebQQ Data Struct API
 * 
 * 
 */

#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <stdlib.h>

#include "type.h"
#include "smemory.h"
#include "logger.h"
#include "msg.h"
#include "async.h"
#include "http.h"
#include "internal.h"
#include "utility.h"

LWQQ_EXPORT
const LwqqFeatures lwqq_features = 0
#ifdef WITH_LIBEV
|LWQQ_WITH_LIBEV
#endif
#ifdef WITH_LIBUV
|LWQQ_WITH_LIBUV
#endif
#ifdef WITH_SQLITE
|LWQQ_WITH_SQLITE
#endif
#ifdef WITH_MOZJS
|LWQQ_WITH_MOZJS
#endif
#ifdef SSL
|LWQQ_WITH_SSL
#endif
;

LWQQ_EXPORT 
const char* lwqq_version = LWQQ_VERSION;

#define HASH_ENTRY_SIZE 8

#define key_eq(a,b) (a==b)
#define val_eq(a,b) (strcmp(a,b)==0)
PAIR_BEGIN_LONG(lwqq_status, int, const char*)
	PAIR(LWQQ_STATUS_ONLINE , "online")
	PAIR(LWQQ_STATUS_OFFLINE, "offline")
	PAIR(LWQQ_STATUS_AWAY   , "away")
	PAIR(LWQQ_STATUS_HIDDEN , "hidden")
	PAIR(LWQQ_STATUS_BUSY   , "busy")
	PAIR(LWQQ_STATUS_CALLME , "callme")
	PAIR(LWQQ_STATUS_SLIENT , "slient")
PAIR_END(lwqq_status, int, const char*, LWQQ_STATUS_LOGOUT, NULL)

static char* generate_random_id(int length)
{
	static long seed = 0;
	char* range = "1234567890abcdefghijklmnopqrstuvwxyz";
	int range_len = strlen(range);
	srand(time(0)+ ++seed);
	char* s = s_malloc0(length+1);
	s[0] = 'D';
	int i;
	for(i=1;i<length;i++)
		s[i] = range[rand()%range_len];
	return s;
}

LWQQ_EXPORT
const char* lwqq_status_to_str(LwqqStatus status)
{
	return lwqq_status_to_val(status);
}

LWQQ_EXPORT
LwqqStatus lwqq_status_from_str(const char* str)
{
	return lwqq_status_to_key(str);
}

typedef struct LwqqClient_
{
	LwqqClient parent;
	LwqqHttpHandle* http;
	LwqqHashEntry hash_entry[HASH_ENTRY_SIZE];
	LwqqHashEntry* hash_beg;
	LwqqHashEntry* hash_idx;
	int hash_next; /* if first call hash_auto, it shouldn't goto next. if isn't,
							it should try next entry */
}LwqqClient_;

/** 
 * Create a new lwqq client
 * 
 * @param username QQ username
 * @param password QQ password
 * 
 * @return A new LwqqClient instance, or NULL failed
 */
LWQQ_EXPORT
LwqqClient *lwqq_client_new(const char *username, const char *password)
{
	setlocale(LC_TIME,"en_US.utf8");///< use at get avatar

	if (!username || !password) {
		lwqq_log(LOG_ERROR, "Username or password is null\n");
		return NULL;
	}

	LwqqClient *lc = s_malloc0(sizeof(LwqqClient_));
	lc->magic = LWQQ_MAGIC;
	lc->username = s_strdup(username);
	lc->password = s_strdup(password);
	lc->myself = lwqq_buddy_new();
	if (!lc->myself) {
		goto failed;
	}
	lc->myself->qqnumber = s_strdup(username);
	lc->myself->uin = s_strdup(username);

	lc->msg_list = lwqq_msglist_new(lc);

	LwqqFriendCategory* my_friend = s_malloc0(sizeof(*my_friend));
	my_friend->index = 0;
	my_friend->name = s_strdup("My Friend");
	LIST_INSERT_HEAD(&lc->categories, my_friend, entries);

	lc->events = s_malloc0(sizeof(*lc->events));
	lc->args = s_malloc0(sizeof(*lc->args));

	lc->find_buddy_by_uin = lwqq_buddy_find_buddy_by_uin;
	lc->find_buddy_by_qqnumber = lwqq_buddy_find_buddy_by_qqnumber;
	lwqq_async_init(lc);

	LwqqClient_* lc_ = (LwqqClient_*) lc;
	lwqq_hash_add_entry(lc, "hashN", lwqq_util_hashN, NULL);
	lwqq_hash_add_entry(lc, "hashO", lwqq_util_hashO, NULL);
	lwqq_hash_add_entry(lc, "hashP", lwqq_util_hashP, NULL);
	lwqq_hash_add_entry(lc, "hashQ", lwqq_util_hashQ, NULL);
	lc_->http = lwqq_http_handle_new();
	lc_->hash_beg = lc_->hash_idx = lc_->hash_entry;

	return lc;

failed:
	lwqq_client_free(lc);
	return NULL;
}

LWQQ_EXPORT
struct LwqqHttpHandle* lwqq_get_http_handle(LwqqClient* lc)
{
	return ((LwqqClient_*)lc)->http;
}

void lwqq_vc_free(LwqqVerifyCode *vc)
{
	if (vc) {
		s_free(vc->str);
		s_free(vc->uin);
		s_free(vc->data);
		s_free(vc);
	}
}

static void lwqq_categories_free(LwqqFriendCategory *cate)
{
	if (!cate)
		return ;

	s_free(cate->name);
	s_free(cate);
}


/** 
 * Free LwqqClient instance
 * 
 * @param client LwqqClient instance
 */
LWQQ_EXPORT
void lwqq_client_free(LwqqClient *client)
{
	LwqqClient_* lc_ = (LwqqClient_*) client;
	LwqqBuddy *b_entry, *b_next;
	LwqqFriendCategory *c_entry, *c_next;
	LwqqGroup *g_entry, *g_next;
	LwqqGroup* d_entry,* d_next;

	if (!client)
		return ;

	//important remove all http request
	lwqq_http_cleanup(client, LWQQ_CLEANUP_WAITALL);
	lwqq_http_handle_free(lc_->http);

	/* Free LwqqVerifyCode instance */
	s_free(client->username);
	s_free(client->password);
	s_free(client->version);
	s_free(client->error_description);
	lwqq_vc_free(client->vc);
	s_free(client->clientid);
	s_free(client->seskey);
	s_free(client->cip);
	s_free(client->index);
	s_free(client->port);
	s_free(client->vfwebqq);
	s_free(client->psessionid);
	s_free(client->login_sig);
	s_free(client->session.ptwebqq);
	s_free(client->session.pt_verifysession);
	lwqq_buddy_free(client->myself);

	//remove all extensions
	vp_do_repeat(client->events->ext_clean, NULL);

	vp_cancel(client->events->login_complete);
	vp_cancel(client->events->poll_msg);
	vp_cancel(client->events->poll_lost);
	vp_cancel(client->events->upload_fail);
	vp_cancel(client->events->new_friend);
	vp_cancel(client->events->new_group);
	vp_cancel(client->events->need_verify);
	vp_cancel(client->events->delete_group);
	vp_cancel(client->events->group_member_chg);
	vp_cancel(client->events->ext_clean);
	vp_cancel(client->events->friend_chg);
	vp_cancel(client->events->group_chg);
	s_free(client->events);
	s_free(client->args);

	/* Free friends list */
	LIST_FOREACH_SAFE(b_entry, &client->friends, entries, b_next) {
		LIST_REMOVE(b_entry, entries);
		lwqq_buddy_free(b_entry);
	}

	/* Free categories list */
	LIST_FOREACH_SAFE(c_entry, &client->categories, entries, c_next) {
		LIST_REMOVE(c_entry, entries);
		lwqq_categories_free(c_entry);
	}


	/* Free groups list */
	LIST_FOREACH_SAFE(g_entry, &client->groups, entries, g_next) {
		LIST_REMOVE(g_entry, entries);
		lwqq_group_free(g_entry);
	}

	LIST_FOREACH_SAFE(d_entry, &client->discus, entries, d_next) {
		LIST_REMOVE(d_entry, entries);
		lwqq_group_free(d_entry);
	}

	/* Free msg_list */
	lwqq_msglist_close(client->msg_list);
	client->magic = 0;
	s_free(client);
}

/************************************************************************/
/* LwqqBuddy API */

/** 
 * 
 * Create a new buddy
 * 
 * @return A LwqqBuddy instance
 */
LWQQ_EXPORT
LwqqBuddy *lwqq_buddy_new()
{
	LwqqBuddy *b = s_malloc0(sizeof(*b));
	b->stat = LWQQ_STATUS_OFFLINE;
	b->client_type = LWQQ_CLIENT_PC;
	return b;
}

/** 
 * Free a LwqqBuddy instance
 * 
 * @param buddy 
 */
LWQQ_EXPORT
void lwqq_buddy_free(LwqqBuddy *buddy)
{
	if (!buddy)
		return ;

	s_free(buddy->uin);
	s_free(buddy->qqnumber);
	s_free(buddy->face);
	s_free(buddy->occupation);
	s_free(buddy->phone);
	s_free(buddy->allow);
	s_free(buddy->college);
	s_free(buddy->reg_time);
	s_free(buddy->homepage);
	s_free(buddy->country);
	s_free(buddy->city);
	s_free(buddy->personal);
	s_free(buddy->nick);
	s_free(buddy->long_nick);
	s_free(buddy->email);
	s_free(buddy->province);
	s_free(buddy->mobile);
	s_free(buddy->vip_info);
	s_free(buddy->markname);
	s_free(buddy->flag);

	s_free(buddy->avatar);
	s_free(buddy->token);

	s_free(buddy);
}

LWQQ_EXPORT
LwqqSimpleBuddy* lwqq_simple_buddy_new()
{
	LwqqSimpleBuddy*ret = ((LwqqSimpleBuddy*)s_malloc0(sizeof(LwqqSimpleBuddy)));
	ret->stat = LWQQ_STATUS_OFFLINE;
	return ret;
}

LWQQ_EXPORT
void lwqq_simple_buddy_free(LwqqSimpleBuddy* buddy)
{
	if(!buddy) return;

	s_free(buddy->uin);
	s_free(buddy->qq);
	s_free(buddy->cate_index);
	s_free(buddy->nick);
	s_free(buddy->card);
	//s_free(buddy->stat);
	s_free(buddy->group_sig);
	s_free(buddy);
}

/** 
 * Find buddy object by buddy's uin member
 * 
 * @param lc Our Lwqq client object
 * @param uin The uin of buddy which we want to find
 * 
 * @return 
 */
LWQQ_EXPORT
LwqqBuddy *lwqq_buddy_find_buddy_by_uin(LwqqClient *lc, const char *uin)
{
	LwqqBuddy *buddy;

	if (!lc || !uin)
		return NULL;

	LIST_FOREACH(buddy, &lc->friends, entries) {
		if (buddy->uin && (strcmp(buddy->uin, uin) == 0))
			return buddy;
	}

	return NULL;
}

LWQQ_EXPORT
LwqqBuddy *lwqq_buddy_find_buddy_by_qqnumber(LwqqClient *lc, const char *qqnum)
{
	LwqqBuddy* buddy;
	LIST_FOREACH(buddy,&lc->friends,entries) {
		//this may caused by qqnumber not loaded successful.
		if(buddy->qqnumber && strcmp(buddy->qqnumber,qqnum)==0)
			return buddy;
	}
	return NULL;
}

LWQQ_EXPORT
LwqqBuddy* lwqq_buddy_find_buddy_by_name(LwqqClient* lc,const char* name)
{
	if(!lc || !name ) return NULL;
	LwqqBuddy * buddy = NULL;
	LIST_FOREACH(buddy,&lc->friends,entries){
		if((buddy->markname && !strcmp(buddy->markname,name) )||
				(buddy->nick && !strcmp(buddy->nick,name)))
			return buddy;
	}
	return NULL;
}

LWQQ_EXPORT
LwqqFriendCategory* lwqq_category_find_by_name(LwqqClient* lc,const char* name)
{
	if(!lc||!name) return NULL;
	LwqqFriendCategory* cate;
	LIST_FOREACH(cate,&lc->categories,entries){
		if(cate->name && !strcmp(cate->name,name)) return cate;
	}
	return NULL;
}

LWQQ_EXPORT
LwqqFriendCategory* lwqq_category_find_by_id(LwqqClient* lc,int index)
{
	if(!lc) return NULL;
	LwqqFriendCategory* cate;
	LIST_FOREACH(cate,&lc->categories,entries){
		if(cate->index == index) return cate;
	}
	return NULL;
}

/* LwqqBuddy API END*/
/************************************************************************/

/** 
 * Create a new group
 * 
 * @return A LwqqGroup instance
 */
LWQQ_EXPORT
LwqqGroup *lwqq_group_new(int type)
{
	LwqqGroup *g = s_malloc0(sizeof(*g));
	if (type == 0) g->type = LWQQ_GROUP_QUN;
	else{
		g->type = LWQQ_GROUP_DISCU;
		g->account = generate_random_id(9);
	}
	return g;
}

/** 
 * Free a LwqqGroup instance
 * 
 * @param group
 */
LWQQ_EXPORT
void lwqq_group_free(LwqqGroup *group)
{
	LwqqSimpleBuddy *m_entry, *m_next;
	if (!group)
		return ;

	s_free(group->name);
	s_free(group->gid);
	s_free(group->code);
	s_free(group->account);
	s_free(group->markname);
	s_free(group->face);
	s_free(group->memo);
	s_free(group->classes);
	s_free(group->fingermemo);
	s_free(group->level);
	s_free(group->owner);
	s_free(group->flag);
	s_free(group->option);

	s_free(group->avatar);

	/* Free Group members list */
	LIST_FOREACH_SAFE(m_entry, &group->members, entries, m_next) {
		LIST_REMOVE(m_entry, entries);
		lwqq_simple_buddy_free(m_entry);
	}

	s_free(group);
}


/** 
 * Find group object by group's gid member
 * 
 * @param lc Our Lwqq client object
 * @param uin The gid of group which we want to find
 * 
 * @return A LwqqGroup instance 
 */
LWQQ_EXPORT
LwqqGroup *lwqq_group_find_group_by_gid(LwqqClient *lc, const char *gid)
{
	LwqqGroup *group;

	if (!lc || !gid)
		return NULL;

	LIST_FOREACH(group, &lc->groups, entries) {
		if (group->gid && (strcmp(group->gid, gid) == 0))
			return group;
	}
	LIST_FOREACH(group, &lc->discus, entries) {
		if (group->did && (strcmp(group->did, gid) == 0))
			return group;
	}

	return NULL;
}

LWQQ_EXPORT
LwqqGroup* lwqq_group_find_group_by_qqnumber(LwqqClient* lc,const char* qqnumber)
{
	LwqqGroup *group,*discu;

	if (!lc || !qqnumber)
		return NULL;

	LIST_FOREACH(group,&lc->groups,entries){
		if(group->account && ! strcmp(group->account,qqnumber) ) return group;
	}

	LIST_FOREACH(discu,&lc->discus,entries){
		if(discu->account && ! strcmp(discu->account,qqnumber) ) return discu;
	}
	return NULL;
}

/** 
 * Find group member object by member's uin
 * 
 * @param group Our Lwqq group object
 * @param uin The uin of group member which we want to find
 * 
 * @return A LwqqBuddy instance 
 */
LWQQ_EXPORT
LwqqSimpleBuddy *lwqq_group_find_group_member_by_uin(LwqqGroup *group, const char *uin)
{
	LwqqSimpleBuddy *member;

	if (!group || !uin)
		return NULL;

	LIST_FOREACH(member, &group->members, entries) {
		if (member->uin && (strcmp(member->uin, uin) == 0))
			return member;
	}

	return NULL;
}

LWQQ_EXPORT
const char* lwqq_date_to_str(time_t date)
{
	static char buf[128];
	memset(buf,0,sizeof(buf));
	if(date == 0 || date == -1) return buf;
	struct tm *tm_ = localtime(&date);
	strftime(buf,sizeof(buf),"%Y-%m-%d",tm_);
	return buf;
}

long lwqq_time()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	long ret;
	ret = tv.tv_sec*1000+tv.tv_usec/1000;
	return ret;
}

LWQQ_EXPORT
const LwqqCommand* lwqq_add_event_listener(LwqqCommand* inko,LwqqCommand cmd)
{
	return vp_link(inko, &cmd);
}

LWQQ_EXPORT
char* lwqq_hash_auto(const char* uin, const char* ptwebqq, void* lc)
{
	LwqqClient_* lc_ = lc;
	if(lc_->hash_next){
		lc_->hash_idx++;
		if(lc_->hash_idx->name == NULL) lc_->hash_idx = lc_->hash_entry;
	}
	lwqq_verbose(2, "[using hash: %s]\n", lc_->hash_idx->name);
	lc_->hash_next = 1;
	return lc_->hash_idx->func(uin,ptwebqq,lc_->hash_idx->data);
}

LWQQ_EXPORT
int lwqq_hash_all_finished(LwqqClient* lc)
{
	if(!lc) return 1;// always tring stop iteration
	LwqqClient_* lc_ = (LwqqClient_*)lc;
	LwqqHashEntry* entry = lc_->hash_beg;
	if(entry == lc_->hash_entry) { // if idx point at begin
		for(entry = lc_->hash_entry + HASH_ENTRY_SIZE -1; entry >lc_->hash_entry; --entry){
			// we find the last element
			if(entry->name) break;
		}
	}else
		--entry;
	// if hash_idx == hash_beg-1; then we have tried all hash 
	return lc_->hash_idx == entry;
}

LWQQ_EXPORT
void lwqq_hash_add_entry(LwqqClient* lc, const char* name, LwqqHashFunc func, void* data)
{
	if(!lc || !name || !func) return;
	LwqqClient_* lc_ = (LwqqClient_*)lc;
	LwqqHashEntry* entry;
	for(entry = lc_->hash_entry ; entry != lc_->hash_entry + HASH_ENTRY_SIZE -1; entry++){
		if(entry->name == NULL){
			entry->name = name;
			entry->func = func;
			entry->data = data;
			break;
		}
	}
}

LWQQ_EXPORT
void lwqq_hash_set_beg(LwqqClient* lc, const char* hash_name)
{
	LwqqClient_* lc_ = (LwqqClient_*)lc;
	if(lc==NULL) return;
	LwqqHashEntry* entry;
	lc_->hash_next = 0;
	if(hash_name == NULL) return; // only clear hash_next
	for(entry = lc_->hash_entry ; entry != lc_->hash_entry + HASH_ENTRY_SIZE; entry++){
		if(entry->name == NULL) break;
		if(strcmp(entry->name, hash_name) == 0){
			lc_->hash_idx = lc_->hash_beg = entry;
			break;
		}
	}
}

LWQQ_EXPORT
const LwqqHashEntry* lwqq_hash_get_last(LwqqClient* lc)
{
	if(!lc) return NULL;
	LwqqClient_* lc_ = (LwqqClient_*)lc;
	return lc_->hash_idx;
}

// vim: ts=3 sw=3 sts=3 noet

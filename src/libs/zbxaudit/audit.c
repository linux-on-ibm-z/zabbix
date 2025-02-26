/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "log.h"
#include "zbxjson.h"
#include "dbcache.h"

#include "audit.h"

#define	AUDIT_DETAILS_KEY_LEN		100

#define AUDIT_RESOURCE_HOST		4
#define AUDIT_RESOURCE_SCRIPT		25

static int		audit_mode;
static zbx_hashset_t	zbx_audit;

static void	append_str_json(struct zbx_json *json, const char *audit_op, const char *key, const char *val)
{
	zbx_json_addarray(json, key);
	zbx_json_addstring(json, NULL, audit_op, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, NULL, val, ZBX_JSON_TYPE_STRING);
	zbx_json_close(json);
}

static void	append_uint64_json(struct zbx_json *json, const char *audit_op, const char *key, const uint64_t val)
{
	zbx_json_addarray(json, key);
	zbx_json_addstring(json, NULL, audit_op, ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(json, NULL, val);
	zbx_json_close(json);
}

static void	append_int_json(struct zbx_json *json, const char *audit_op, const char *key, int val)
{
	zbx_json_addarray(json, key);
	zbx_json_addstring(json, NULL, audit_op, ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, NULL, val);
	zbx_json_close(json);
}

static void	update_str_json(struct zbx_json *json, const char *key, const char *val_old, const char *val_new)
{
	zbx_json_addarray(json, key);
	zbx_json_addstring(json, NULL, "update", ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, NULL, val_new, ZBX_JSON_TYPE_STRING);
	zbx_json_addstring(json, NULL, val_old, ZBX_JSON_TYPE_STRING);
	zbx_json_close(json);
}

static void	update_uint64_json(struct zbx_json *json, const char *key, uint64_t val_old, uint64_t val_new)
{
	zbx_json_addarray(json, key);
	zbx_json_addstring(json, NULL, "update", ZBX_JSON_TYPE_STRING);
	zbx_json_adduint64(json, NULL, val_new);
	zbx_json_adduint64(json, NULL, val_old);
	zbx_json_close(json);
}

static void	update_int_json(struct zbx_json *json, const char *key, int val_old, int val_new)
{
	zbx_json_addarray(json, key);
	zbx_json_addstring(json, NULL, "update", ZBX_JSON_TYPE_STRING);
	zbx_json_addint64(json, NULL, val_new);
	zbx_json_addint64(json, NULL, val_old);
	zbx_json_close(json);
}

#define RETURN_IF_AUDIT_OFF()				\
	if (ZBX_AUDITLOG_ENABLED != audit_mode)		\
		return					\

/******************************************************************************
 *                                                                            *
 * Function: zbx_auditlog_global_script                                       *
 *                                                                            *
 * Purpose: record global script execution results into audit log             *
 *                                                                            *
 * Comments: 'hostid' should be always > 0. 'eventid' is > 0 in case of       *
 *           "manual script on event"                                         *
 *                                                                            *
 ******************************************************************************/
int	zbx_auditlog_global_script(unsigned char script_type, unsigned char script_execute_on,
		const char *script_command_orig, zbx_uint64_t hostid, const char *hostname, zbx_uint64_t eventid,
		zbx_uint64_t proxy_hostid, zbx_uint64_t userid, const char *username, const char *clientip,
		const char *output, const char *error)
{
	int	ret = SUCCEED;
	char	auditid_cuid[CUID_LEN], execute_on_s[MAX_ID_LEN + 1], hostid_s[MAX_ID_LEN + 1],
		eventid_s[MAX_ID_LEN + 1], proxy_hostid_s[MAX_ID_LEN + 1];

	struct zbx_json	details_json;
	zbx_config_t	cfg;

	zabbix_log(LOG_LEVEL_TRACE, "In %s()", __func__);

	zbx_config_get(&cfg, ZBX_CONFIG_FLAGS_AUDITLOG_ENABLED);

	if (ZBX_AUDITLOG_ENABLED != cfg.auditlog_enabled)
		goto out;

	zbx_new_cuid(auditid_cuid);

	zbx_json_init(&details_json, ZBX_JSON_STAT_BUF_LEN);

	zbx_snprintf(execute_on_s, sizeof(execute_on_s), "%hhu", script_execute_on);

	append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.execute_on", execute_on_s);

	if (0 != eventid)
	{
		zbx_snprintf(eventid_s, sizeof(eventid_s), ZBX_FS_UI64, eventid);
		append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.eventid", eventid_s);
	}

	zbx_snprintf(hostid_s, sizeof(hostid_s), ZBX_FS_UI64, hostid);
	append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.hostid", hostid_s);

	if (0 != proxy_hostid)
	{
		zbx_snprintf(proxy_hostid_s, sizeof(proxy_hostid_s), ZBX_FS_UI64, proxy_hostid);
		append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.proxy_hostid", proxy_hostid_s);
	}

	if (ZBX_SCRIPT_TYPE_WEBHOOK != script_type)
		append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.command", script_command_orig);

	if (NULL != output)
	{
		char	*output_esc;

		output_esc = zbx_strdup(NULL, output);
		zbx_json_escape(&output_esc);
		append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.output", output_esc);
		zbx_free(output_esc);
	}

	if (NULL != error)
		append_str_json(&details_json, AUDIT_DETAILS_ACTION_ADD, "script.error", error);

	if (ZBX_DB_OK > DBexecute("insert into auditlog (auditid,userid,username,clock,action,ip,resourceid,"
			"resourcename,resourcetype,recordsetid,details) values ('%s'," ZBX_FS_UI64 ",'%s',%d,'%d','%s',"
			ZBX_FS_UI64 ",'%s',%d,'%s','%s')", auditid_cuid, userid, username, (int)time(NULL),
			AUDIT_ACTION_EXECUTE, clientip, hostid, hostname, AUDIT_RESOURCE_SCRIPT, auditid_cuid,
			details_json.buffer))
	{
		ret = FAIL;
	}

	zbx_json_free(&details_json);
out:
	zabbix_log(LOG_LEVEL_TRACE, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

typedef struct zbx_audit_entry
{
	zbx_uint64_t	id;
	char		*name;
	struct zbx_json	details_json;
	int		audit_action;
	int		resource_type;
} zbx_audit_entry_t;

static unsigned	zbx_audit_hash_func(const void *data)
{
	const zbx_audit_entry_t	* const *audit_entry = (const zbx_audit_entry_t * const *)data;

	return ZBX_DEFAULT_UINT64_HASH_ALGO(&((*audit_entry)->id), sizeof((*audit_entry)->id),
			ZBX_DEFAULT_HASH_SEED);
}

static int	zbx_audit_compare_func(const void *d1, const void *d2)
{
	const zbx_audit_entry_t	* const *audit_entry_1 = (const zbx_audit_entry_t * const *)d1;
	const zbx_audit_entry_t	* const *audit_entry_2 = (const zbx_audit_entry_t * const *)d2;

	ZBX_RETURN_IF_NOT_EQUAL((*audit_entry_1)->id, (*audit_entry_2)->id);

	return 0;
}

static void	zbx_audit_clean(void)
{
	zbx_hashset_iter_t	iter;
	zbx_audit_entry_t	**audit_entry;

	RETURN_IF_AUDIT_OFF();

	zbx_hashset_iter_reset(&zbx_audit, &iter);

	while (NULL != (audit_entry = (zbx_audit_entry_t **)zbx_hashset_iter_next(&iter)))
	{
		zbx_json_free(&((*audit_entry)->details_json));
		zbx_free((*audit_entry)->name);
		zbx_free(*audit_entry);
	}

	zbx_hashset_destroy(&zbx_audit);
}

void	zbx_audit_init(int audit_mode_set)
{
	audit_mode = audit_mode_set;
	RETURN_IF_AUDIT_OFF();
#define AUDIT_HASHSET_DEF_SIZE	100
	zbx_hashset_create(&zbx_audit, AUDIT_HASHSET_DEF_SIZE, zbx_audit_hash_func, zbx_audit_compare_func);
#undef AUDIT_HASHSET_DEF_SIZE
}

void	zbx_audit_flush(void)
{
	char			audit_cuid[CUID_LEN], recsetid_cuid[CUID_LEN];
	zbx_hashset_iter_t	iter;
	zbx_audit_entry_t	**audit_entry;
	zbx_db_insert_t		db_insert_audit;

	RETURN_IF_AUDIT_OFF();

	zbx_new_cuid(recsetid_cuid);
	zbx_hashset_iter_reset(&zbx_audit, &iter);

	zbx_db_insert_prepare(&db_insert_audit, "auditlog", "auditid", "userid", "username", "clock", "action", "ip",
			"resourceid", "resourcename", "resourcetype", "recordsetid", "details", NULL);

	while (NULL != (audit_entry = (zbx_audit_entry_t **)zbx_hashset_iter_next(&iter)))
	{
		zbx_new_cuid(audit_cuid);
		if (AUDIT_ACTION_DELETE == (*audit_entry)->audit_action ||
				0 != strcmp((*audit_entry)->details_json.buffer, "{}"))
		{
#define AUDIT_USERID	0
#define AUDIT_USERNAME	"System"
#define AUDIT_IP	""
			zbx_db_insert_add_values(&db_insert_audit, audit_cuid, AUDIT_USERID, AUDIT_USERNAME,
					(int)time(NULL), (*audit_entry)->audit_action, AUDIT_IP, (*audit_entry)->id,
					(*audit_entry)->name, (*audit_entry)->resource_type,
					recsetid_cuid, (*audit_entry)->details_json.buffer);
#undef AUDIT_USERID
#undef AUDIT_USERNAME
#undef AUDIT_IP
		}
	}

	zbx_db_insert_execute(&db_insert_audit);
	zbx_db_insert_clean(&db_insert_audit);

	zbx_audit_clean();
}

void	zbx_audit_update_json_append_string(const zbx_uint64_t id, const char *audit_op, const char *key,
		const char *value)
{
	zbx_audit_entry_t	local_audit_entry, **found_audit_entry;
	zbx_audit_entry_t	*local_audit_entry_x = &local_audit_entry;

	local_audit_entry.id = id;

	found_audit_entry = (zbx_audit_entry_t**)zbx_hashset_search(&zbx_audit, &(local_audit_entry_x));

	if (NULL == found_audit_entry)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	append_str_json(&((*found_audit_entry)->details_json), audit_op, key, value);
}

void	zbx_audit_update_json_append_uint64(const zbx_uint64_t id, const char *audit_op, const char *key,
		uint64_t value)
{
	zbx_audit_entry_t	local_audit_entry, **found_audit_entry;
	zbx_audit_entry_t	*local_audit_entry_x = &local_audit_entry;

	local_audit_entry.id = id;

	found_audit_entry = (zbx_audit_entry_t**)zbx_hashset_search(&zbx_audit, &(local_audit_entry_x));

	if (NULL == found_audit_entry)
	{
		THIS_SHOULD_NEVER_HAPPEN;
		exit(EXIT_FAILURE);
	}

	append_uint64_json(&((*found_audit_entry)->details_json), audit_op, key, value);
}

#define PREPARE_UPDATE_JSON_APPEND_OP(...)					\
	zbx_audit_entry_t	local_audit_entry, **found_audit_entry;		\
	zbx_audit_entry_t	*local_audit_entry_x = &local_audit_entry;	\
										\
	local_audit_entry.id = id;						\
										\
	found_audit_entry = (zbx_audit_entry_t**)zbx_hashset_search(&zbx_audit,	\
			&(local_audit_entry_x));				\
	if (NULL == found_audit_entry)						\
	{									\
		THIS_SHOULD_NEVER_HAPPEN;					\
		exit(EXIT_FAILURE);						\
	}									\

void	zbx_audit_update_json_append_int(const zbx_uint64_t id, const char *audit_op, const char *key, int value)
{
	PREPARE_UPDATE_JSON_APPEND_OP();
	append_int_json(&((*found_audit_entry)->details_json), audit_op, key, value);
}

void	zbx_audit_update_json_update_string(const zbx_uint64_t id, const char *key, const char *value_old,
		const char *value_new)
{
	PREPARE_UPDATE_JSON_APPEND_OP();
	update_str_json(&((*found_audit_entry)->details_json), key, value_old, value_new);
}

void	zbx_audit_update_json_update_uint64(const zbx_uint64_t id, const char *key, uint64_t value_old,
		uint64_t value_new)
{
	PREPARE_UPDATE_JSON_APPEND_OP();
	update_uint64_json(&((*found_audit_entry)->details_json), key, value_old, value_new);
}

void	zbx_audit_update_json_update_int(const zbx_uint64_t id, const char *key, int value_old,
		int value_new)
{
	PREPARE_UPDATE_JSON_APPEND_OP();
	update_int_json(&((*found_audit_entry)->details_json), key, value_old, value_new);
}

void	zbx_audit_host_update_json_add_interfaces(zbx_uint64_t hostid, zbx_uint64_t interfaceid,
		zbx_uint64_t main_, zbx_uint64_t type, zbx_uint64_t useip, const char *ip, const char *dns,
		zbx_uint64_t port)
{
	char	audit_key_main[AUDIT_DETAILS_KEY_LEN], audit_key_type[AUDIT_DETAILS_KEY_LEN],
		audit_key_useip[AUDIT_DETAILS_KEY_LEN], audit_key_ip[AUDIT_DETAILS_KEY_LEN],
		audit_key_dns[AUDIT_DETAILS_KEY_LEN], audit_key_port[AUDIT_DETAILS_KEY_LEN];
	RETURN_IF_AUDIT_OFF();

	zbx_snprintf(audit_key_main,  AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].main", interfaceid);
	zbx_snprintf(audit_key_type,  AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].type", interfaceid);
	zbx_snprintf(audit_key_useip, AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].useip", interfaceid);
	zbx_snprintf(audit_key_ip,    AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].ip", interfaceid);
	zbx_snprintf(audit_key_dns,   AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].dns", interfaceid);
	zbx_snprintf(audit_key_port,  AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].port", interfaceid);

	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_main, main_);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_type, type);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_useip, useip);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_ip, ip);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_dns, dns);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_port, port);
}

void	zbx_audit_host_update_json_update_interface_useip(zbx_uint64_t hostid, zbx_uint64_t interfaceid,
		zbx_uint64_t useip_old, zbx_uint64_t useip_new)
{
	char	buf[AUDIT_DETAILS_KEY_LEN];

	RETURN_IF_AUDIT_OFF();

	zbx_snprintf(buf, sizeof(buf), "host.interfaces[%lu].useip", interfaceid);
	zbx_audit_update_json_update_uint64(hostid, buf, useip_old, useip_new);
}

void	zbx_audit_host_update_json_update_interface_ip(zbx_uint64_t hostid, zbx_uint64_t interfaceid,
		const char *ip_old, const char *ip_new)
{
	char	buf[AUDIT_DETAILS_KEY_LEN];

	RETURN_IF_AUDIT_OFF();

	zbx_snprintf(buf, sizeof(buf), "host.interfaces[%lu].ip", interfaceid);
	zbx_audit_update_json_update_string(hostid, buf, ip_old, ip_new);
}

void	zbx_audit_host_update_json_update_interface_dns(zbx_uint64_t hostid, zbx_uint64_t interfaceid,
		const char *dns_old, const char *dns_new)
{
	char	buf[AUDIT_DETAILS_KEY_LEN];

	RETURN_IF_AUDIT_OFF();

	zbx_snprintf(buf, sizeof(buf), "host.interfaces[%lu].dns", interfaceid);
	zbx_audit_update_json_update_string(hostid, buf, dns_old, dns_new);
}

void	zbx_audit_host_update_json_update_interface_port(zbx_uint64_t hostid, zbx_uint64_t interfaceid,
		zbx_uint64_t port_old, zbx_uint64_t port_new)
{
	char	buf[AUDIT_DETAILS_KEY_LEN];

	RETURN_IF_AUDIT_OFF();

	zbx_snprintf(buf, sizeof(buf), "host.interfaces[%lu].port", interfaceid);
	zbx_audit_update_json_update_uint64(hostid, buf, port_old, port_new);
}

#define PREPARE_UPDATE_JSON_SNMP_INTERFACE_OP(...)								\
	char	audit_key_version[AUDIT_DETAILS_KEY_LEN], audit_key_bulk[AUDIT_DETAILS_KEY_LEN],		\
		audit_key_community[AUDIT_DETAILS_KEY_LEN], audit_key_securityname[AUDIT_DETAILS_KEY_LEN],	\
		audit_key_securitylevel[AUDIT_DETAILS_KEY_LEN], audit_key_authpassphrase[AUDIT_DETAILS_KEY_LEN],\
		audit_key_privpassphrase[AUDIT_DETAILS_KEY_LEN], audit_key_authprotocol[AUDIT_DETAILS_KEY_LEN],	\
		audit_key_privprotocol[AUDIT_DETAILS_KEY_LEN], audit_key_contextname[AUDIT_DETAILS_KEY_LEN];	\
	RETURN_IF_AUDIT_OFF();											\
														\
	zbx_snprintf(audit_key_version,  AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].details.version",		\
			interfaceid);										\
	zbx_snprintf(audit_key_bulk,  AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].details.bulk", interfaceid);	\
	zbx_snprintf(audit_key_community, AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].details.community",	\
			interfaceid);										\
	zbx_snprintf(audit_key_securityname,    AUDIT_DETAILS_KEY_LEN,						\
			"host.interfaces[%lu].details.securityname", interfaceid);				\
	zbx_snprintf(audit_key_securitylevel,   AUDIT_DETAILS_KEY_LEN,						\
			"host.interfaces[%lu].details.securitylevel", interfaceid);				\
	zbx_snprintf(audit_key_authpassphrase,  AUDIT_DETAILS_KEY_LEN,						\
			"host.interfaces[%lu].details.authpassphrase", interfaceid);				\
	zbx_snprintf(audit_key_privpassphrase,  AUDIT_DETAILS_KEY_LEN,						\
			"host.interfaces[%lu].details.privpassphrase", interfaceid);				\
	zbx_snprintf(audit_key_authprotocol,  AUDIT_DETAILS_KEY_LEN,						\
			"host.interfaces[%lu].details.authprotocol", interfaceid);				\
	zbx_snprintf(audit_key_privprotocol,  AUDIT_DETAILS_KEY_LEN,						\
			"host.interfaces[%lu].details.privprotocol", interfaceid);				\
	zbx_snprintf(audit_key_contextname,  AUDIT_DETAILS_KEY_LEN, "host.interfaces[%lu].details.contextname",	\
			interfaceid);										\

void	zbx_audit_host_update_json_add_snmp_interface(zbx_uint64_t hostid, zbx_uint64_t version, zbx_uint64_t bulk,
		const char *community, const char *securityname, zbx_uint64_t securitylevel, const char *authpassphrase,
		const char *privpassphrase, zbx_uint64_t authprotocol, zbx_uint64_t privprotocol,
		const char *contextname, zbx_uint64_t interfaceid)
{
PREPARE_UPDATE_JSON_SNMP_INTERFACE_OP()
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_version, version);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_bulk, bulk);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_community, community);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_securityname, securityname);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_securitylevel, securitylevel);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_authpassphrase, authpassphrase);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_privpassphrase, privpassphrase);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_authprotocol, authprotocol);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_privprotocol, privprotocol);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, audit_key_contextname, contextname);
}

void	zbx_audit_host_update_json_update_snmp_interface(zbx_uint64_t hostid, zbx_uint64_t version_old,
		zbx_uint64_t version_new, zbx_uint64_t bulk_old,  zbx_uint64_t bulk_new, const char *community_old,
		const char *community_new, const char *securityname_old, const char *securityname_new,
		zbx_uint64_t securitylevel_old, zbx_uint64_t securitylevel_new, const char *authpassphrase_old,
		const char *authpassphrase_new, const char *privpassphrase_old, const char *privpassphrase_new,
		zbx_uint64_t authprotocol_old, zbx_uint64_t authprotocol_new, zbx_uint64_t privprotocol_old,
		zbx_uint64_t privprotocol_new, const char *contextname_old, const char *contextname_new,
		zbx_uint64_t interfaceid)
{
PREPARE_UPDATE_JSON_SNMP_INTERFACE_OP()
	zbx_audit_update_json_update_uint64(hostid, audit_key_version, version_old, version_new);
	zbx_audit_update_json_update_uint64(hostid, audit_key_bulk, bulk_old, bulk_new);
	zbx_audit_update_json_update_string(hostid, audit_key_community, community_old, community_new);
	zbx_audit_update_json_update_string(hostid, audit_key_securityname, securityname_old, securityname_new);
	zbx_audit_update_json_update_uint64(hostid, audit_key_securitylevel, securitylevel_old, securitylevel_new);
	zbx_audit_update_json_update_string(hostid, audit_key_authpassphrase, authpassphrase_old, authpassphrase_new);
	zbx_audit_update_json_update_string(hostid, audit_key_privpassphrase, privpassphrase_old, privpassphrase_new);
	zbx_audit_update_json_update_uint64(hostid, audit_key_authprotocol, authprotocol_old, authprotocol_new);
	zbx_audit_update_json_update_uint64(hostid, audit_key_privprotocol, privprotocol_old, privprotocol_new);
	zbx_audit_update_json_update_string(hostid, audit_key_contextname, contextname_old,  contextname_new);
}

void	zbx_audit_host_update_json_add_proxy_hostid_and_hostname(zbx_uint64_t hostid,
		zbx_uint64_t proxy_hostid, const char *hostname)
{
	RETURN_IF_AUDIT_OFF();

	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ADD, "host.proxy_hostid", proxy_hostid);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, "host.host", hostname);
}

void	zbx_audit_host_update_json_add_tls_and_psk(zbx_uint64_t hostid, int tls_connect,
		int tls_accept, const char *psk_identity, const char *psk)
{
	RETURN_IF_AUDIT_OFF();

	zbx_audit_update_json_append_int(hostid, AUDIT_DETAILS_ACTION_ADD, "host.tls_connect", tls_connect);
	zbx_audit_update_json_append_int(hostid, AUDIT_DETAILS_ACTION_ADD, "host.tls_accept", tls_accept);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, "host.psk_identity", psk_identity);
	zbx_audit_update_json_append_string(hostid, AUDIT_DETAILS_ACTION_ADD, "host.psk", psk);
}

void	zbx_audit_host_update_json_add_inventory_mode(zbx_uint64_t hostid, int inventory_mode)
{
	RETURN_IF_AUDIT_OFF();

	zbx_audit_update_json_append_int(hostid, AUDIT_DETAILS_ACTION_ADD, "host.inventory_mode", inventory_mode);
}

void	zbx_audit_host_update_json_update_inventory_mode(zbx_uint64_t hostid, int inventory_mode_old,
		int inventory_mode_new)
{
	RETURN_IF_AUDIT_OFF();

	zbx_audit_update_json_update_int(hostid, "host.inventory_mode", inventory_mode_old, inventory_mode_new);
}

void	zbx_audit_host_update_json_update_host_status(zbx_uint64_t hostid, int host_status_old,
		int host_status_new)
{
	RETURN_IF_AUDIT_OFF();

	zbx_audit_update_json_update_int(hostid, "host.status", host_status_old, host_status_new);
}

void	zbx_audit_host_create_entry(int audit_action, zbx_uint64_t hostid, const char *name)
{
	zbx_audit_entry_t	local_audit_host_entry, **found_audit_host_entry;
	zbx_audit_entry_t	*local_audit_host_entry_x = &local_audit_host_entry;

	RETURN_IF_AUDIT_OFF();

	local_audit_host_entry.id = hostid;

	found_audit_host_entry = (zbx_audit_entry_t**)zbx_hashset_search(&zbx_audit,
			&(local_audit_host_entry_x));
	if (NULL == found_audit_host_entry)
	{
		zbx_audit_entry_t	*local_audit_host_entry_insert;

		local_audit_host_entry_insert = (zbx_audit_entry_t*)zbx_malloc(NULL, sizeof(zbx_audit_entry_t));
		local_audit_host_entry_insert->id = hostid;
		local_audit_host_entry_insert->name = zbx_strdup(NULL, name);
		local_audit_host_entry_insert->audit_action = audit_action;
		local_audit_host_entry_insert->resource_type = AUDIT_RESOURCE_HOST;
		zbx_json_init(&(local_audit_host_entry_insert->details_json), ZBX_JSON_STAT_BUF_LEN);
		zbx_hashset_insert(&zbx_audit, &local_audit_host_entry_insert, sizeof(local_audit_host_entry_insert));
	}
}

void	zbx_audit_hostgroup_update_json_attach(zbx_uint64_t hostid, zbx_uint64_t hostgroupid, zbx_uint64_t groupid)
{
	char	buf[AUDIT_DETAILS_KEY_LEN];

	RETURN_IF_AUDIT_OFF();

	zbx_snprintf(buf, sizeof(buf), "host.groups[%lu]", hostgroupid);
	zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_ATTACH, buf, groupid);
}

void	zbx_audit_host_hostgroup_delete(zbx_uint64_t hostid, const char* hostname, zbx_vector_uint64_t *hostgroupids,
		zbx_vector_uint64_t *groupids)
{
	char	buf[AUDIT_DETAILS_KEY_LEN];
	int	i;

	RETURN_IF_AUDIT_OFF();

	zbx_audit_host_create_entry(AUDIT_ACTION_UPDATE, hostid, hostname);

	for (i = 0; i < groupids->values_num; i++)
	{
		zbx_snprintf(buf, sizeof(buf), "host.groups[%lu]", hostgroupids->values[i]);
		zbx_audit_update_json_append_uint64(hostid, AUDIT_DETAILS_ACTION_DETACH, buf,
				groupids->values[i]);
	}
}

void	zbx_audit_host_del(zbx_uint64_t hostid, const char *hostname)
{
	RETURN_IF_AUDIT_OFF();

	zbx_audit_host_create_entry(AUDIT_ACTION_DELETE, hostid, hostname);
}

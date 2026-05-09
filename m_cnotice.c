#include "unrealircd.h"

const char *translate_service_nick = "MechaSqueak[BOT]";

// Forward declarations
CMD_FUNC(cmd_cnotice);
CMD_FUNC(cmd_msgas);

ModuleHeader MOD_HEADER = {"third/m_cnotice", "1.0",
                           "Channel-Scoped Private NOTICE (CNOTICE)",
                           "Alex Sørlie", "unrealircd-6"};

MOD_INIT() {
  CommandAdd(modinfo->handle, "CNOTICE", cmd_cnotice, 3, CMD_USER);
  CommandAdd(modinfo->handle, "MSGAS", cmd_msgas, 4, CMD_USER | CMD_SERVER);
  return MOD_SUCCESS;
}

MOD_LOAD() { return MOD_SUCCESS; }

MOD_UNLOAD() { return MOD_SUCCESS; }

#if UnrealProtocol < 4200
#define BANPERMISSION "tkl:zline:global"
#else
#define BANPERMISSION "server-ban:zline:global"
#endif

/* sidenote: sendnotice() and sendtxtnumeric() assume no client or server
 * has a % in their nick, which is a safe assumption since % is illegal.
 */

/** Send a server notice to a client.
 * @param to            The client to send to
 * @param pattern       The format string / pattern to use.
 * @param ...           Format string parameters.
 */
void sendcnotice(Client *to, Client *from, Channel *channel,
                 const char *message) {
  sendto_prefix_one(to, from, NULL, ":%s NOTICE %s :%s", from->name,
                    channel->name, message);
}

CMD_FUNC(cmd_cnotice) {
  const char *target_nick, *channel_name, *message;
  Client *target;
  Channel *channel;
  Membership *m;

  if (!MyUser(client))
    return;

  if (parc < 3) {
    sendnotice(client, "Usage: /CNOTICE <nick> <#channel> <message>");
    return;
  }

  target_nick = parv[1];
  channel_name = parv[2];
  message = parv[3];

  // Check if the channel exists
  channel = find_channel(channel_name);
  if (!channel) {
    sendnotice(client, "CNOTICE: Channel %s does not exist.", channel_name);
    return;
  }

  // Check if the target exists
  target = find_client(target_nick, NULL);
  if (!target || !IsUser(target)) {
    sendnotice(client, "CNOTICE: No such user %s.", target_nick);
    return;
  }

  // Check if the sender is in the channel and has +o or higher
  if (!check_channel_access(client, channel, "hoaq") && !IsULine(client)) {
    if (ValidatePermissionsForPath("channel:override:invite:invite-only",
                                   client, NULL, channel, NULL) &&
        client == target) {
    } else {
      sendnumeric(client, ERR_CHANOPRIVSNEEDED, channel->name);
      return;
    }
  }
  Membership *lp = find_membership_link(target->user->channel, channel);
  if (!lp) {
    if (MyUser(client))
      sendnumeric(client, ERR_USERNOTINCHANNEL, target_nick, channel_name);

    return;
  }
  sendcnotice(target, client, channel, message);
}

CMD_FUNC(cmd_msgas) {
  const char *src_nick, *target_name, *message;
  Client *src_user;

  if (IsUser(client) && !IsULine(client) && strcmp(client->name, translate_service_nick) != 0) {
    sendnumeric(client, ERR_NOPRIVILEGES);
    return;
  }
  if (parc < 4) {
    sendnotice(client, "Usage: /MSGAS <nick> <#channel|nick> <message>");
    return;
  }

  src_nick = parv[1];
  target_name = parv[2];
  message = parv[3];

  src_user = find_client(src_nick, NULL);
  if ((!src_user || !IsUser(src_user)) && (*target_name == '#' || *target_name == '&' || *target_name == '+')) {
    /* Source must be online for channel messages */
    sendnotice(client, "MSGAS: No such user %s.", src_nick);
    return;
  }

  /* Check if target is a channel or a user */
  if (*target_name == '#' || *target_name == '&' || *target_name == '+') {
    /* Channel target — existing behavior */
    Channel *channel;

    if (!MyUser(src_user)) {
      if (!IsUser(client)) {
        return;
      }
      sendto_server(NULL, 0, 0, NULL, ":%s MSGAS %s %s %s", client->id,
                    src_nick, target_name, message);
      return;
    }

    channel = find_channel(target_name);
    if (!channel) {
      sendnotice(client, "MSGAS: Channel %s does not exist.", target_name);
      return;
    }

    Membership *lp = find_membership_link(src_user->user->channel, channel);
    if (!lp) {
      if (MyUser(client))
        sendnumeric(client, ERR_USERNOTINCHANNEL, src_user->name, target_name);
      return;
    }

    MessageTag *mtags = NULL;
    new_message(src_user, recv_mtags, &mtags);
    sendto_channel(channel, src_user, NULL, NULL, 0, SEND_ALL, mtags,
                   ":%s PRIVMSG %s :%s", src_user->name, channel->name,
                   message);
    free_message_tags(mtags);

    /* Only log channel messages, not PMs */
    unreal_log(
        ULOG_INFO, "sacmds", "MSGAS_COMMAND", client,
        "MSGAS: $issuer used MSGAS to make $target speak in $channel: $msg",
        log_data_string("issuer", client->name),
        log_data_client("target", src_user),
        log_data_string("channel", target_name),
        log_data_string("msg", message));
  } else {
    /* User target — send a PM appearing from src_user */
    Client *target = find_user(target_name, NULL);
    if (!target) {
      return;
    }

    if (!MyUser(target)) {
      sendto_server(NULL, 0, 0, NULL, ":%s MSGAS %s %s :%s", client->id,
                    src_nick, target_name, message);
      return;
    }

    /* For PM targets, check if we have a timestamp and message (5 params)
     * or just a message (4 params).
     * 5 params: MSGAS <src> <target> <timestamp> :<message>
     * 4 params: MSGAS <src> <target> :<message>
     */
    const char *pm_message;
    MessageTag *mtags = NULL;

    if (parc >= 5 && parv[4] && *parv[4]) {
      /* Has timestamp in parv[3], message in parv[4] */
      pm_message = parv[4];
      MessageTag *m = safe_alloc(sizeof(MessageTag));
      safe_strdup(m->name, "time");
      safe_strdup(m->value, parv[3]);
      m->next = mtags;
      mtags = m;
    } else {
      pm_message = message; /* parv[3] is the message */
    }

    if (src_user && IsUser(src_user)) {
      /* Source is online — send from them */
      if (!mtags)
        new_message(src_user, recv_mtags, &mtags);
      sendto_prefix_one(target, src_user, mtags, ":%s PRIVMSG %s :%s",
                        src_user->name, target->name, pm_message);
    } else {
      /* Source is offline — construct a fake prefix */
      sendto_prefix_one(target, NULL, mtags, ":%s!offline@fuelrats.com PRIVMSG %s :%s",
                        src_nick, target->name, pm_message);
    }
    free_message_tags(mtags);
  }
}



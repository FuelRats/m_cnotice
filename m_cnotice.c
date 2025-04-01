#include "unrealircd.h"

const char *translate_service_nick = "MechaSqueak[BOT]";

// Forward declarations
CMD_FUNC(cmd_cnotice);
CMD_FUNC(cmd_msgas);
CMD_FUNC(cmd_translate);

ModuleHeader MOD_HEADER = {"third/m_cnotice", "1.0",
                           "Channel-Scoped Private NOTICE (CNOTICE)",
                           "Alex Sørlie", "unrealircd-6"};

MOD_INIT() {
  CommandAdd(modinfo->handle, "CNOTICE", cmd_cnotice, 3, CMD_USER);
  CommandAdd(modinfo->handle, "MSGAS", cmd_msgas, 3, CMD_OPER | CMD_SERVER);
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
  if (!check_channel_access(client, channel, "oaq") && !IsULine(client)) {
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
  const char *src_nick, *channel_name, *message;
  Client *src_user;
  Channel *channel;

  if (IsUser(client) && strcmp(client->name, translate_service_nick) != 0) {
    sendnumeric(client, ERR_NOPRIVILEGES);
    return;
  }
  if (parc < 4) {
    sendnotice(client, "Usage: /MSGAS <nick> <#channel> <message>");
    return;
  }

  src_nick = parv[1];
  channel_name = parv[2];
  message = parv[3];

  src_user = find_client(src_nick, NULL);
  if (!src_user || !IsUser(src_user)) {
    sendnotice(client, "MSGAS: No such user %s.", src_nick);
    return;
  }
  if (!MyUser(src_user)) {
    if (!IsUser(client)) {
      return;
    }
    sendto_server(NULL, 0, 0, NULL, ":%s MSGAS %s %s %s", client->id, src_nick,
                  channel_name, message);
    return;
  }

  channel = find_channel(channel_name);
  if (!channel) {
    sendnotice(client, "MSGAS: Channel %s does not exist.", channel_name);
    return;
  }

  Membership *lp = find_membership_link(src_user->user->channel, channel);
  if (!lp) {
    if (MyUser(client))
      sendnumeric(client, ERR_USERNOTINCHANNEL, src_user->name, channel_name);

    return;
  }

  MessageTag *mtags = NULL;
  new_message(src_user, recv_mtags, &mtags);
  sendto_channel(channel, src_user, NULL, NULL, 0, SEND_ALL, mtags,
                 ":%s PRIVMSG %s :%s", src_user->name, channel->name, message);

  unreal_log(
      ULOG_INFO, "sacmds", "MSGAS_COMMAND", client,
      "MSGAS: $issuer used MSGAS to make $target speak in $channel: $msg",
      log_data_string("issuer", client->name),
      log_data_client("target", src_user),
      log_data_string("channel", channel->name),
      log_data_string("msg", message));
}


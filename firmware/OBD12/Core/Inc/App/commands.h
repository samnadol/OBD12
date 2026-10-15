/*
 * commands.h
 *
 * Host protocol command handlers (commands_handle() is declared in
 * Comms/comms.h). Also owns the live-data subscriptions and deferred actions
 * such as CMD_RESET.
 */

#ifndef INC_APP_COMMANDS_H_
#define INC_APP_COMMANDS_H_

// a host that started live data must send some request (e.g. CMD_PING) at
// least this often, otherwise its live subscription lapses; this stops CAN
// polling when a host goes away without sending CMD_LIVE_STOP
#define LIVE_LEASE_MS 30000

void commands_process(void);

#endif /* INC_APP_COMMANDS_H_ */

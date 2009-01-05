#include <opensrf/osrf_stack.h>
#include <opensrf/osrf_application.h>

/* the max number of oilsMessage blobs present in any one root packet */
#define OSRF_MAX_MSGS_PER_PACKET 256
// -----------------------------------------------------------------------------

static int  osrf_stack_process( transport_client* client, int timeout, int* msg_received );
static void osrf_stack_application_handler( osrfAppSession* session, osrfMessage* msg );
static void _do_client( osrfAppSession*, osrfMessage* );
static void _do_server( osrfAppSession*, osrfMessage* );

/* tell osrfAppSession where the stack entry is */
int (*osrf_stack_entry_point) (transport_client*, int, int*)  = &osrf_stack_process;

static int osrf_stack_process( transport_client* client, int timeout, int* msg_received ) {
	if( !client ) return -1;
	transport_message* msg = NULL;
	if(msg_received) *msg_received = 0;

	while( (msg = client_recv( client, timeout )) ) {
		if(msg_received) *msg_received = 1;
		osrfLogDebug( OSRF_LOG_MARK,  "Received message from transport code from %s", msg->sender );
		osrf_stack_transport_handler( msg, NULL );
		timeout = 0;
	}

	if( client->error ) {
		osrfLogWarning(OSRF_LOG_MARK, "transport_client had trouble reading from the socket..");
		return -1;
	}

	if( ! client_connected( client ) ) return -1;

	return 0;
}



// -----------------------------------------------------------------------------
// Entry point into the stack
// -----------------------------------------------------------------------------
osrfAppSession* osrf_stack_transport_handler( transport_message* msg,
		const char* my_service ) {

	if(!msg) return NULL;

   osrfLogSetXid(msg->osrf_xid);

	osrfLogDebug( OSRF_LOG_MARK,  "Transport handler received new message \nfrom %s "
			"to %s with body \n\n%s\n", msg->sender, msg->recipient, msg->body );

	if( msg->is_error && ! msg->thread ) {
		osrfLogWarning( OSRF_LOG_MARK, "!! Received jabber layer error for %s ... exiting\n", msg->sender );
		message_free( msg );
		return NULL;
	}

	if(! msg->thread  && ! msg->is_error ) {
		osrfLogWarning( OSRF_LOG_MARK, "Received a non-error message with no thread trace... dropping");
		message_free( msg );
		return NULL;
	}

	osrfAppSession* session = osrf_app_session_find_session( msg->thread );

	if( !session && my_service ) 
		session = osrf_app_server_session_init( msg->thread, my_service, msg->sender);

	if( !session ) {
		message_free( msg );
		return NULL;
	}
	
	if(!msg->is_error)
		osrfLogDebug( OSRF_LOG_MARK, "Session [%s] found or built", session->session_id );

	osrf_app_session_set_remote( session, msg->sender );
	osrfMessage* arr[OSRF_MAX_MSGS_PER_PACKET];

	/* Convert the message body into one or more osrfMessages */
	int num_msgs = osrf_message_deserialize(msg->body, arr, OSRF_MAX_MSGS_PER_PACKET);

	osrfLogDebug( OSRF_LOG_MARK,  "We received %d messages from %s", num_msgs, msg->sender );

	double starttime = get_timestamp_millis();

	int i;
	for( i = 0; i < num_msgs; i++ ) {

		/* if we've received a jabber layer error message (probably talking to 
			someone who no longer exists) and we're not talking to the original
			remote id for this server, consider it a redirect and pass it up */
		if(msg->is_error) {
			osrfLogWarning( OSRF_LOG_MARK,  " !!! Received Jabber layer error message" ); 

			if(strcmp(session->remote_id,session->orig_remote_id)) {
				osrfLogWarning( OSRF_LOG_MARK,  "Treating jabber error as redirect for tt [%d] "
					"and session [%s]", arr[i]->thread_trace, session->session_id );

				arr[i]->m_type = STATUS;
				arr[i]->status_code = OSRF_STATUS_REDIRECTED;

			} else {
				osrfLogWarning( OSRF_LOG_MARK, " * Jabber Error is for top level remote "
                    " id [%s], no one to send my message to!  Cutting request short...", session->remote_id );
                session->transport_error = 1;
                break;
			}
		}

		if( session->type ==  OSRF_SESSION_CLIENT )
			_do_client( session, arr[i] );
		else
			_do_server( session, arr[i] );
	}

	double duration = get_timestamp_millis() - starttime;
	osrfLogInfo(OSRF_LOG_MARK, "Message processing duration %f", duration);

	message_free( msg );
	osrfLogDebug( OSRF_LOG_MARK, "after msg delete");

	return session;
}

/** If we return a message, that message should be passed up the stack,
  * if we return NULL, we're finished for now...
  */
static void _do_client( osrfAppSession* session, osrfMessage* msg ) {
	if(session == NULL || msg == NULL)
		return;

	osrfMessage* further_msg = NULL;

	if( msg->m_type == STATUS ) {
		
		switch( msg->status_code ) {

			case OSRF_STATUS_OK:
				osrfLogDebug( OSRF_LOG_MARK, "We connected successfully");
				session->state = OSRF_SESSION_CONNECTED;
				osrfLogDebug( OSRF_LOG_MARK,  "State: %x => %s => %d", session, session->session_id, session->state );
				break;

			case OSRF_STATUS_COMPLETE:
				osrf_app_session_set_complete( session, msg->thread_trace );
				break;

			case OSRF_STATUS_CONTINUE:
				osrf_app_session_request_reset_timeout( session, msg->thread_trace );
				break;

			case OSRF_STATUS_REDIRECTED:
				osrf_app_session_reset_remote( session );
				session->state = OSRF_SESSION_DISCONNECTED;
				osrf_app_session_request_resend( session, msg->thread_trace );
				break;

			case OSRF_STATUS_EXPFAILED: 
				osrf_app_session_reset_remote( session );
				session->state = OSRF_SESSION_DISCONNECTED;
				break;

			case OSRF_STATUS_TIMEOUT:
				osrf_app_session_reset_remote( session );
				session->state = OSRF_SESSION_DISCONNECTED;
				osrf_app_session_request_resend( session, msg->thread_trace );
				break;

			default:
				/* Replace the old message with a new one */
				further_msg = osrf_message_init( RESULT, msg->thread_trace, msg->protocol );
				osrf_message_set_status_info( further_msg,
						msg->status_name, msg->status_text, msg->status_code );
				osrfLogWarning( OSRF_LOG_MARK, "The stack doesn't know what to do with " 
						"the provided message code: %d, name %s. Passing UP.", 
						msg->status_code, msg->status_name );
				further_msg->is_exception = 1;
				osrf_app_session_set_complete( session, msg->thread_trace );
				osrfMessageFree(msg);
				break;
		}

	} else if( msg->m_type == RESULT ) {
		further_msg = msg;
	}

	if(further_msg) {
		osrfLogDebug( OSRF_LOG_MARK, "passing client message %d / session %s to app handler",
					  msg->thread_trace, session->session_id );
		osrf_stack_application_handler( session, further_msg );
	}

	if(msg != further_msg)
		osrfMessageFree(msg);

	return;
}


/** If we return a message, that message should be passed up the stack, 
  * if we return NULL, we're finished for now...
  */
static void _do_server( osrfAppSession* session, osrfMessage* msg ) {

	if(session == NULL || msg == NULL) return;

	osrfLogDebug( OSRF_LOG_MARK, "Server received message of type %d", msg->m_type );

	osrfMessage* further_msg = NULL;
	
	switch( msg->m_type ) {

		case STATUS:
			break;

		case DISCONNECT:
			/* session will be freed by the forker */
			osrfLogDebug(OSRF_LOG_MARK, "Client sent explicit disconnect");
			session->state = OSRF_SESSION_DISCONNECTED;
			break;

		case CONNECT:
			osrfAppSessionStatus( session, OSRF_STATUS_OK,
					"osrfConnectStatus", msg->thread_trace, "Connection Successful" );
			session->state = OSRF_SESSION_CONNECTED;
			break;

		case REQUEST:

			osrfLogDebug( OSRF_LOG_MARK, "server passing message %d to application handler "
					"for session %s", msg->thread_trace, session->session_id );
			further_msg = msg;
			break;

		default:
			osrfLogWarning( OSRF_LOG_MARK, "Server cannot handle message of type %d", msg->m_type );
			session->state = OSRF_SESSION_DISCONNECTED;
			break;
	}

	if(further_msg) {
		osrfLogDebug( OSRF_LOG_MARK, "passing server message %d / session %s to app handler",
					  msg->thread_trace, session->session_id );
		osrf_stack_application_handler( session, further_msg );
	}

	if(msg != further_msg)
		osrfMessageFree(msg);

	return;
}



static void osrf_stack_application_handler( osrfAppSession* session, osrfMessage* msg ) {
	if(session == NULL || msg == NULL) return;

	if(msg->m_type == RESULT && session->type == OSRF_SESSION_CLIENT) {
		/* Enqueue the RESULT message to be processed later */
		osrf_app_session_push_queue( session, msg );
	}
	else if(msg->m_type == REQUEST) {
		char* method = msg->method_name;
		char* app	= session->remote_service;
		jsonObject* params = msg->_params;

		osrfAppRunMethod( app, method,  session, msg->thread_trace, params );
		osrfMessageFree(msg);
	}
	
	return;
}



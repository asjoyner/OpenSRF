#include <opensrf/osrf_message.h>

static char default_locale[17] = "en-US\0\0\0\0\0\0\0\0\0\0\0\0";
static char* current_locale = NULL;

osrfMessage* osrf_message_init( enum M_TYPE type, int thread_trace, int protocol ) {

	osrfMessage* msg			= (osrfMessage*) safe_malloc(sizeof(osrfMessage));
	msg->m_type					= type;
	msg->thread_trace			= thread_trace;
	msg->protocol				= protocol;
	msg->status_name			= NULL;
	msg->status_text			= NULL;
	msg->status_code			= 0;
	msg->next					= NULL;
	msg->is_exception			= 0;
	msg->_params				= NULL;
	msg->_result_content		= NULL;
	msg->result_string			= NULL;
	msg->method_name			= NULL;
	msg->full_param_string		= NULL;
	msg->sender_locale		= NULL;
	msg->sender_tz_offset		= 0;

	return msg;
}


const char* osrf_message_get_last_locale() {
	return current_locale;
}

char* osrf_message_set_locale( osrfMessage* msg, const char* locale ) {
	if( msg == NULL || locale == NULL ) return NULL;
	return msg->sender_locale = strdup( locale );
}

const char* osrf_message_set_default_locale( const char* locale ) {
	if( locale == NULL ) return NULL;
	if( strlen(locale) > sizeof(default_locale) - 1 ) return NULL;

	strcpy( default_locale, locale );
	return (const char*) default_locale;
}

void osrf_message_set_method( osrfMessage* msg, const char* method_name ) {
	if( msg == NULL || method_name == NULL ) return;
	msg->method_name = strdup( method_name );
}


void osrf_message_add_object_param( osrfMessage* msg, const jsonObject* o ) {
	if(!msg|| !o) return;
	if(!msg->_params)
		msg->_params = jsonParseString("[]");
	char* j = jsonObjectToJSON(o);
	jsonObjectPush(msg->_params, jsonParseString(j));
	free(j);
}

void osrf_message_set_params( osrfMessage* msg, const jsonObject* o ) {
	if(!msg || !o) return;

	if(o->type != JSON_ARRAY) {
		osrfLogDebug( OSRF_LOG_MARK, "passing non-array to osrf_message_set_params(), fixing...");
		if(msg->_params) jsonObjectFree(msg->_params);
		jsonObject* clone = jsonObjectClone(o);
		msg->_params = jsonNewObject(NULL);
		jsonObjectPush(msg->_params, clone);
		return;
	}

	if(msg->_params) jsonObjectFree(msg->_params);
	msg->_params = jsonObjectClone(o);
}


/* only works if parse_json_params is false */
void osrf_message_add_param( osrfMessage* msg, const char* param_string ) {
	if(msg == NULL || param_string == NULL) return;
	if(!msg->_params) msg->_params = jsonParseString("[]");
	jsonObjectPush(msg->_params, jsonParseString(param_string));
}


void osrf_message_set_status_info( osrfMessage* msg,
		const char* status_name, const char* status_text, int status_code ) {
	if(!msg) return;

	if( status_name != NULL ) 
		msg->status_name = strdup( status_name );

	if( status_text != NULL )
		msg->status_text = strdup( status_text );

	msg->status_code = status_code;
}


void osrf_message_set_result_content( osrfMessage* msg, const char* json_string ) {
	if( msg == NULL || json_string == NULL) return;
	msg->result_string =	strdup(json_string);
	if(json_string) msg->_result_content = jsonParseString(json_string);
}



void osrfMessageFree( osrfMessage* msg ) {
	osrf_message_free( msg );
}

void osrf_message_free( osrfMessage* msg ) {
	if( msg == NULL )
		return;

	if( msg->status_name != NULL )
		free(msg->status_name);

	if( msg->status_text != NULL )
		free(msg->status_text);

	if( msg->_result_content != NULL )
		jsonObjectFree( msg->_result_content );

	if( msg->result_string != NULL )
		free( msg->result_string);

	if( msg->method_name != NULL )
		free(msg->method_name);

	if( msg->sender_locale != NULL )
		free(msg->sender_locale);

	if( msg->_params != NULL )
		jsonObjectFree(msg->_params);

	free(msg);
}


char* osrfMessageSerializeBatch( osrfMessage* msgs [], int count ) {
	if( !msgs ) return NULL;

	char* j;
	int i = 0;
	const osrfMessage* msg = NULL;
	jsonObject* wrapper = jsonNewObject(NULL);

	while( ((msg = msgs[i]) && (i++ < count)) ) 
		jsonObjectPush(wrapper, osrfMessageToJSON( msg ));

	j = jsonObjectToJSON(wrapper);
	jsonObjectFree(wrapper);

	return j;	
}


char* osrf_message_serialize(const osrfMessage* msg) {

	if( msg == NULL ) return NULL;
	char* j = NULL;

	jsonObject* json = osrfMessageToJSON( msg );

	if(json) {
		jsonObject* wrapper = jsonNewObject(NULL);
		jsonObjectPush(wrapper, json);
		j = jsonObjectToJSON(wrapper);
		jsonObjectFree(wrapper);
	}

	return j;
}


jsonObject* osrfMessageToJSON( const osrfMessage* msg ) {

	jsonObject* json = jsonNewObject(NULL);
	jsonObjectSetClass(json, "osrfMessage");
	jsonObject* payload;
	char sc[64];
	osrf_clearbuf(sc, sizeof(sc));

	char* str;

	INT_TO_STRING(msg->thread_trace);
	jsonObjectSetKey(json, "threadTrace", jsonNewObject(INTSTR));

	if (msg->sender_locale != NULL) {
		jsonObjectSetKey(json, "locale", jsonNewObject(msg->sender_locale));
	} else if (current_locale != NULL) {
		jsonObjectSetKey(json, "locale", jsonNewObject(current_locale));
	} else {
		jsonObjectSetKey(json, "locale", jsonNewObject(default_locale));
	}

	switch(msg->m_type) {
		
		case CONNECT: 
			jsonObjectSetKey(json, "type", jsonNewObject("CONNECT"));
			break;

		case DISCONNECT: 
			jsonObjectSetKey(json, "type", jsonNewObject("DISCONNECT"));
			break;

		case STATUS:
			jsonObjectSetKey(json, "type", jsonNewObject("STATUS"));
			payload = jsonNewObject(NULL);
			jsonObjectSetClass(payload, msg->status_name);
			jsonObjectSetKey(payload, "status", jsonNewObject(msg->status_text));
			snprintf(sc, sizeof(sc), "%d", msg->status_code);
			jsonObjectSetKey(payload, "statusCode", jsonNewObject(sc));
			jsonObjectSetKey(json, "payload", payload);
			break;

		case REQUEST:
			jsonObjectSetKey(json, "type", jsonNewObject("REQUEST"));
			payload = jsonNewObject(NULL);
			jsonObjectSetClass(payload, "osrfMethod");
			jsonObjectSetKey(payload, "method", jsonNewObject(msg->method_name));
			str = jsonObjectToJSON(msg->_params);
			jsonObjectSetKey(payload, "params", jsonParseString(str));
			free(str);
			jsonObjectSetKey(json, "payload", payload);

			break;

		case RESULT:
			jsonObjectSetKey(json, "type", jsonNewObject("RESULT"));
			payload = jsonNewObject(NULL);
			jsonObjectSetClass(payload,"osrfResult");
			jsonObjectSetKey(payload, "status", jsonNewObject(msg->status_text));
			snprintf(sc, sizeof(sc), "%d", msg->status_code);
			jsonObjectSetKey(payload, "statusCode", jsonNewObject(sc));
			str = jsonObjectToJSON(msg->_result_content);
			jsonObjectSetKey(payload, "content", jsonParseString(str));
			free(str);
			jsonObjectSetKey(json, "payload", payload);
			break;
	}

	return json;
}


int osrf_message_deserialize(const char* string, osrfMessage* msgs[], int count) {

	if(!string || !msgs || count <= 0) return 0;
	int numparsed = 0;

	jsonObject* json = jsonParseString(string);

	if(!json) {
		osrfLogWarning( OSRF_LOG_MARK, 
			"osrf_message_deserialize() unable to parse data: \n%s\n", string);
		return 0;
	}

	int x;

	for( x = 0; x < json->size && x < count; x++ ) {

		const jsonObject* message = jsonObjectGetIndex(json, x);

		if(message && message->type != JSON_NULL && 
			message->classname && !strcmp(message->classname, "osrfMessage")) {

			osrfMessage* new_msg = safe_malloc(sizeof(osrfMessage));
			new_msg->m_type = 0;
			new_msg->thread_trace = 0;
			new_msg->protocol = 0;
			new_msg->status_name = NULL;
			new_msg->status_text = NULL;
			new_msg->status_code = 0;
			new_msg->is_exception = 0;
			new_msg->_result_content = NULL;
			new_msg->result_string = NULL;
			new_msg->method_name = NULL;
			new_msg->_params = NULL;
			new_msg->next = NULL;
			new_msg->full_param_string = NULL;
			new_msg->sender_locale = NULL;
			new_msg->sender_tz_offset = 0;

			const jsonObject* tmp = jsonObjectGetKeyConst(message, "type");

			const char* t;
			if( ( t = jsonObjectGetString(tmp)) ) {

				if(!strcmp(t, "CONNECT")) 		new_msg->m_type = CONNECT;
				else if(!strcmp(t, "DISCONNECT")) 	new_msg->m_type = DISCONNECT;
				else if(!strcmp(t, "STATUS")) 		new_msg->m_type = STATUS;
				else if(!strcmp(t, "REQUEST")) 		new_msg->m_type = REQUEST;
				else if(!strcmp(t, "RESULT")) 		new_msg->m_type = RESULT;
			}

			tmp = jsonObjectGetKeyConst(message, "threadTrace");
			if(tmp) {
				char* tt = jsonObjectToSimpleString(tmp);
				if(tt) {
					new_msg->thread_trace = atoi(tt);
					free(tt);
				}
			}

			/* use the sender's locale, or the global default */
			if (current_locale)
				free( current_locale );

			tmp = jsonObjectGetKeyConst(message, "locale");

			if(tmp && (new_msg->sender_locale = jsonObjectToSimpleString(tmp))) {
				current_locale = strdup( new_msg->sender_locale );
			} else {
				current_locale = NULL;
			}

			tmp = jsonObjectGetKeyConst(message, "protocol");

			if(tmp) {
				char* proto = jsonObjectToSimpleString(tmp);
				if(proto) {
					new_msg->protocol = atoi(proto);
					free(proto);
				}
			}

			tmp = jsonObjectGetKeyConst(message, "payload");
			if(tmp) {
				if(tmp->classname)
					new_msg->status_name = strdup(tmp->classname);

				const jsonObject* tmp0 = jsonObjectGetKeyConst(tmp,"method");
				const char* tmp_str = jsonObjectGetString(tmp0);
				if(tmp_str)
					new_msg->method_name = strdup(tmp_str);

				tmp0 = jsonObjectGetKeyConst(tmp,"params");
				if(tmp0) {
					char* s = jsonObjectToJSON(tmp0);
					new_msg->_params = jsonParseString(s);
					if(new_msg->_params && new_msg->_params->type == JSON_NULL) 
						new_msg->_params->type = JSON_ARRAY;
					free(s);
				}

				tmp0 = jsonObjectGetKeyConst(tmp,"status");
				tmp_str = jsonObjectGetString(tmp0);
				if(tmp_str)
					new_msg->status_text = strdup(tmp_str);

				tmp0 = jsonObjectGetKeyConst(tmp,"statusCode");
				if(tmp0) {
					tmp_str = jsonObjectGetString(tmp0);
					if(tmp_str)
						new_msg->status_code = atoi(tmp_str);
					if(tmp0->type == JSON_NUMBER)
						new_msg->status_code = (int) jsonObjectGetNumber(tmp0);
				}

				tmp0 = jsonObjectGetKeyConst(tmp,"content");
				if(tmp0) {
					char* s = jsonObjectToJSON(tmp0);
					new_msg->_result_content = jsonParseString(s);
					free(s);
				}

			}
			msgs[numparsed++] = new_msg;
		}
	}

	jsonObjectFree(json);
	return numparsed;
}



jsonObject* osrfMessageGetResult( osrfMessage* msg ) {
	if(msg) return msg->_result_content;
	return NULL;
}


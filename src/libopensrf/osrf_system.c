#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>

#include "opensrf/utils.h"
#include "opensrf/log.h"
#include "opensrf/osrf_system.h"
#include "opensrf/osrf_application.h"
#include "opensrf/osrf_prefork.h"

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

static void report_child_status( pid_t pid, int status );
struct child_node;
typedef struct child_node ChildNode;

static void handleKillSignal(int signo) {
    /* we are the top-level process and we've been 
     * killed. Kill all of our children */
    kill(0, SIGTERM);
    sleep(1); /* give the children a chance to die before we reap them */
    pid_t child_pid;
    int status;
    while( (child_pid=waitpid(-1,&status,WNOHANG)) > 0) 
        osrfLogInfo(OSRF_LOG_MARK, "Killed child %d", child_pid);
    _exit(0);
}


struct child_node
{
	ChildNode* pNext;
	ChildNode* pPrev;
	pid_t pid;
	char* app;
	char* libfile;
};

static ChildNode* child_list;

static transport_client* osrfGlobalTransportClient = NULL;

static void add_child( pid_t pid, const char* app, const char* libfile );
static void delete_child( ChildNode* node );
static void delete_all_children( void );
static ChildNode* seek_child( pid_t pid );

transport_client* osrfSystemGetTransportClient( void ) {
	return osrfGlobalTransportClient;
}

void osrfSystemIgnoreTransportClient() {
	osrfGlobalTransportClient = NULL;
}

int osrf_system_bootstrap_client( char* config_file, char* contextnode ) {
	return osrfSystemBootstrapClientResc(config_file, contextnode, NULL);
}

int osrfSystemInitCache( void ) {

	jsonObject* cacheServers = osrf_settings_host_value_object("/cache/global/servers/server");
	char* maxCache = osrf_settings_host_value("/cache/global/max_cache_time");

	if( cacheServers && maxCache) {

		if( cacheServers->type == JSON_ARRAY ) {
			int i;
			const char* servers[cacheServers->size];
			for( i = 0; i != cacheServers->size; i++ ) {
				servers[i] = jsonObjectGetString( jsonObjectGetIndex(cacheServers, i) );
				osrfLogInfo( OSRF_LOG_MARK, "Adding cache server %s", servers[i]);
			}
			osrfCacheInit( servers, cacheServers->size, atoi(maxCache) );

		} else {
			const char* servers[] = { jsonObjectGetString(cacheServers) };		
			osrfLogInfo( OSRF_LOG_MARK, "Adding cache server %s", servers[0]);
			osrfCacheInit( servers, 1, atoi(maxCache) );
		}

	} else {
		osrfLogError( OSRF_LOG_MARK,  "Missing config value for /cache/global/servers/server _or_ "
			"/cache/global/max_cache_time");
	}

	jsonObjectFree( cacheServers );
	return 0;
}


/**
	@brief Set yourself up for business.
	@param hostname Full network name of the host where the process is running; or 
	'localhost' will do.
	@param configfile Name of the configuration file; normally '/openils/conf/opensrf_core.xml'.
	@param contextNode 
	@return - Name of an aggregate within the configuration file, containing the relevant
	subset of configuration stuff.

	
*/
int osrfSystemBootstrap( const char* hostname, const char* configfile,
		const char* contextNode ) {
	if( !(hostname && configfile && contextNode) ) return -1;

	/* first we grab the settings */
	if(!osrfSystemBootstrapClientResc(configfile, contextNode, "settings_grabber" )) {
		osrfLogError( OSRF_LOG_MARK,
			"Unable to bootstrap for host %s from configuration file %s",
			hostname, configfile );
		return -1;
	}

	int retcode = osrf_settings_retrieve(hostname);
	osrf_system_disconnect_client();

	if( retcode ) {
		osrfLogError( OSRF_LOG_MARK,
			"Unable to retrieve settings for host %s from configuration file %s",
			hostname, configfile );
		return -1;
	}

	// Turn into a daemon.  The parent forks and exits.  Only the
	// child returns, with the standard streams (stdin, stdout, and
	// stderr) redirected to /dev/null.
	/* NOTE: This has been moved from below the 'if (apps)' block below ... move it back if things go crazy */
	daemonize();

	jsonObject* apps = osrf_settings_host_value_object("/activeapps/appname");
	osrfStringArray* arr = osrfNewStringArray(8);
	
	if(apps) {
		int i = 0;

		if(apps->type == JSON_STRING) {
			osrfStringArrayAdd(arr, jsonObjectGetString(apps));

		} else {
			const jsonObject* app;
			while( (app = jsonObjectGetIndex(apps, i++)) ) 
				osrfStringArrayAdd(arr, jsonObjectGetString(app));
		}
		jsonObjectFree(apps);

		const char* appname = NULL;
		i = 0;
		while( (appname = osrfStringArrayGetString(arr, i++)) ) {

			char* lang = osrf_settings_host_value("/apps/%s/language", appname);

			if(lang && !strcasecmp(lang,"c"))  {

				char* libfile = osrf_settings_host_value("/apps/%s/implementation", appname);
		
				if(! (appname && libfile) ) {
					osrfLogWarning( OSRF_LOG_MARK, "Missing appname / libfile in settings config");
					continue;
				}

				osrfLogInfo( OSRF_LOG_MARK, "Launching application %s with implementation %s", appname, libfile);
		
				pid_t pid;
		
				if( (pid = fork()) ) { 
					// store pid in local list for re-launching dead children...
					add_child( pid, appname, libfile );
					osrfLogInfo( OSRF_LOG_MARK, "Running application child %s: process id %ld",
								 appname, (long) pid );
	
				} else {
		
					osrfLogInfo( OSRF_LOG_MARK, " * Running application %s\n", appname);
					if( osrfAppRegisterApplication( appname, libfile ) == 0 ) 
						osrf_prefork_run(appname);
	
					osrfLogDebug( OSRF_LOG_MARK, "Server exiting for app %s and library %s\n", appname, libfile );
					exit(0);
				}
			} // language == c
		} 
	} // should we do something if there are no apps? does the wait(NULL) below do that for us?

	osrfStringArrayFree(arr);

    signal(SIGTERM, handleKillSignal);
    signal(SIGINT, handleKillSignal);
	
	while(1) {
		errno = 0;
		int status;
		pid_t pid = wait( &status );
		if(-1 == pid) {
			if(errno == ECHILD)
				osrfLogError(OSRF_LOG_MARK, "We have no more live services... exiting");
			else
				osrfLogError(OSRF_LOG_MARK, "Exiting top-level system loop with error: %s", strerror(errno));
			break;
		} else {
			report_child_status( pid, status );
		}
	}

	delete_all_children();
	return 0;
}


static void report_child_status( pid_t pid, int status )
{
	const char* app;
	const char* libfile;
	ChildNode* node = seek_child( pid );

	if( node ) {
		app     = node->app     ? node->app     : "[unknown]";
		libfile = node->libfile ? node->libfile : "[none]";
	} else
		app = libfile = NULL;
	
	if( WIFEXITED( status ) )
	{
		int rc = WEXITSTATUS( status );  // return code of child process
		if( rc )
			osrfLogError( OSRF_LOG_MARK, "Child process %ld (app %s) exited with return code %d",
						  (long) pid, app, rc );
		else
			osrfLogInfo( OSRF_LOG_MARK, "Child process %ld (app %s) exited normally",
						  (long) pid, app );
	}
	else if( WIFSIGNALED( status ) )
	{
		osrfLogError( OSRF_LOG_MARK, "Child process %ld (app %s) killed by signal %d",
					  (long) pid, app, WTERMSIG( status) );
	}
	else if( WIFSTOPPED( status ) )
	{
		osrfLogError( OSRF_LOG_MARK, "Child process %ld (app %s) stopped by signal %d",
					  (long) pid, app, (int) WSTOPSIG( status ) );
	}

	delete_child( node );
}

/*----------- Routines to manage list of children --*/

static void add_child( pid_t pid, const char* app, const char* libfile )
{
	/* Construct new child node */
	
	ChildNode* node = safe_malloc( sizeof( ChildNode ) );

	node->pid = pid;

	if( app )
		node->app = strdup( app );
	else
		node->app = NULL;

	if( libfile )
		node->libfile = strdup( libfile );
	else
		node->libfile = NULL;
	
	/* Add new child node to the head of the list */

	node->pNext = child_list;
	node->pPrev = NULL;

	if( child_list )
		child_list->pPrev = node;

	child_list = node;
}

static void delete_child( ChildNode* node ) {

	/* Sanity check */

	if( ! node )
		return;
	
	/* Detach the node from the list */

	if( node->pPrev )
		node->pPrev->pNext = node->pNext;
	else
		child_list = node->pNext;

	if( node->pNext )
		node->pNext->pPrev = node->pPrev;

	/* Deallocate the node and its payload */

	free( node->app );
	free( node->libfile );
	free( node );
}

static void delete_all_children( void ) {

	while( child_list )
		delete_child( child_list );
}

static ChildNode* seek_child( pid_t pid ) {

	/* Return a pointer to the child node for the */
	/* specified process ID, or NULL if not found */
	
	ChildNode* node = child_list;
	while( node ) {
		if( node->pid == pid )
			break;
		else
			node = node->pNext;
	}

	return node;
}

/*----------- End of routines to manage list of children --*/


int osrfSystemBootstrapClientResc( const char* config_file,
		const char* contextnode, const char* resource ) {

	int failure = 0;

	if(osrfSystemGetTransportClient()) {
		osrfLogInfo(OSRF_LOG_MARK, "Client is already bootstrapped");
		return 1; /* we already have a client connection */
	}

	if( !( config_file && contextnode ) && ! osrfConfigHasDefaultConfig() ) {
		osrfLogError( OSRF_LOG_MARK, "No Config File Specified\n" );
		return -1;
	}

	if( config_file ) {
		osrfConfig* cfg = osrfConfigInit( config_file, contextnode );
		if(cfg)
			osrfConfigSetDefaultConfig(cfg);
		else
			return 0;   /* Can't load configuration?  Bail out */
	}


	char* log_file		= osrfConfigGetValue( NULL, "/logfile");
	if(!log_file) {
		fprintf(stderr, "No log file specified in configuration file %s\n",
				config_file);
		return -1;
	}

	char* log_level		= osrfConfigGetValue( NULL, "/loglevel" );
	osrfStringArray* arr	= osrfNewStringArray(8);
	osrfConfigGetValueList(NULL, arr, "/domain");

	char* username		= osrfConfigGetValue( NULL, "/username" );
	char* password		= osrfConfigGetValue( NULL, "/passwd" );
	char* port		= osrfConfigGetValue( NULL, "/port" );
	char* unixpath		= osrfConfigGetValue( NULL, "/unixpath" );
	char* facility		= osrfConfigGetValue( NULL, "/syslog" );
	char* actlog		= osrfConfigGetValue( NULL, "/actlog" );

	/* if we're a source-client, tell the logger */
	char* isclient = osrfConfigGetValue(NULL, "/client");
	if( isclient && !strcasecmp(isclient,"true") )
		osrfLogSetIsClient(1);
	free(isclient);

	int llevel = 0;
	int iport = 0;
	if(port) iport = atoi(port);
	if(log_level) llevel = atoi(log_level);

	if(!strcmp(log_file, "syslog")) {
		osrfLogInit( OSRF_LOG_TYPE_SYSLOG, contextnode, llevel );
		osrfLogSetSyslogFacility(osrfLogFacilityToInt(facility));
		if(actlog) osrfLogSetSyslogActFacility(osrfLogFacilityToInt(actlog));

	} else {
		osrfLogInit( OSRF_LOG_TYPE_FILE, contextnode, llevel );
		osrfLogSetFile( log_file );
	}


	/* Get a domain, if one is specified */
	const char* domain = osrfStringArrayGetString( arr, 0 ); /* just the first for now */
	if(!domain) {
		fprintf(stderr, "No domain specified in configuration file %s\n", config_file);
		osrfLogError( OSRF_LOG_MARK, "No domain specified in configuration file %s\n", config_file);
		failure = 1;
	}

	if(!username) {
		fprintf(stderr, "No username specified in configuration file %s\n", config_file);
		osrfLogError( OSRF_LOG_MARK, "No username specified in configuration file %s\n", config_file);
		failure = 1;
	}

	if(!password) {
		fprintf(stderr, "No password specified in configuration file %s\n", config_file);
		osrfLogError( OSRF_LOG_MARK, "No password specified in configuration file %s\n", config_file);
		failure = 1;
	}

	if((iport <= 0) && !unixpath) {
		fprintf(stderr, "No unixpath or valid port in configuration file %s\n", config_file);
		osrfLogError( OSRF_LOG_MARK, "No unixpath or valid port in configuration file %s\n",
			config_file);
		failure = 1;
	}

	if (failure) {
		osrfStringArrayFree(arr);
		free(log_file);
		free(log_level);
		free(username);
		free(password);
		free(port);
		free(unixpath);
		free(facility);
		free(actlog);
		return 0;
	}

	osrfLogInfo( OSRF_LOG_MARK, "Bootstrapping system with domain %s, port %d, and unixpath %s",
		domain, iport, unixpath ? unixpath : "(none)" );
	transport_client* client = client_init( domain, iport, unixpath, 0 );

	char host[HOST_NAME_MAX + 1] = "";
	gethostname(host, sizeof(host) );
	host[HOST_NAME_MAX] = '\0';

	char tbuf[32];
	tbuf[0] = '\0';
	snprintf(tbuf, 32, "%f", get_timestamp_millis());

	if(!resource) resource = "";

	int len = strlen(resource) + 256;
	char buf[len];
	buf[0] = '\0';
	snprintf(buf, len - 1, "%s_%s_%s_%ld", resource, host, tbuf, (long) getpid() );

	if(client_connect( client, username, password, buf, 10, AUTH_DIGEST )) {
		/* child nodes will leak the parents client... but we can't free
			it without disconnecting the parents client :( */
		osrfGlobalTransportClient = client;
	}

	osrfStringArrayFree(arr);
	free(actlog);
	free(facility);
	free(log_level);
	free(log_file);
	free(username);
	free(password);
	free(port);	
	free(unixpath);

	if(osrfGlobalTransportClient)
		return 1;

	return 0;
}

int osrf_system_disconnect_client( void ) {
	client_disconnect( osrfGlobalTransportClient );
	client_free( osrfGlobalTransportClient );
	osrfGlobalTransportClient = NULL;
	return 0;
}

static int shutdownComplete = 0;
int osrf_system_shutdown( void ) {
    if(shutdownComplete) return 0;
	osrfConfigCleanup();
    osrfCacheCleanup();
	osrf_system_disconnect_client();
	osrf_settings_free_host_config(NULL);
	osrfAppSessionCleanup();
	osrfLogCleanup();
    shutdownComplete = 1;
	return 1;
}





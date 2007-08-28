/*
   librc 
   core RC functions
   Copyright 2007 Gentoo Foundation
   Released under the GPLv2
   */

#include "librc.h"

/* usecs to wait while we poll the fifo */
#define WAIT_INTERVAL	20000000

/* max nsecs to wait until a service comes up */
#define WAIT_MAX	60000000000

#define SOFTLEVEL	RC_SVCDIR "/softlevel"

/* File stream used for plugins to write environ vars to */
FILE *rc_environ_fd = NULL;

static const char *rc_service_state_names[] = {
	"started",
	"stopped",
	"starting",
	"stopping",
	"inactive",
	"wasinactive",
	"coldplugged",
	"failed",
	"scheduled",
	NULL
};

bool rc_runlevel_starting (void)
{
	return (rc_is_dir (RC_STARTING));
}
librc_hidden_def(rc_runlevel_starting)

bool rc_runlevel_stopping (void)
{
	return (rc_is_dir (RC_STOPPING));
}
librc_hidden_def(rc_runlevel_stopping)

char **rc_get_runlevels (void)
{
	char **dirs = rc_ls_dir (NULL, RC_RUNLEVELDIR, 0);
	char **runlevels = NULL;
	int i;
	char *dir;

	STRLIST_FOREACH (dirs, dir, i) {
		char *path = rc_strcatpaths (RC_RUNLEVELDIR, dir, (char *) NULL);
		if (rc_is_dir (path))
			runlevels = rc_strlist_addsort (runlevels, dir);
		free (path);
	}
	rc_strlist_free (dirs);

	return (runlevels);
}
librc_hidden_def(rc_get_runlevels)

char *rc_get_runlevel (void)
{
	FILE *fp;
	static char buffer [PATH_MAX];

	if (! (fp = fopen (SOFTLEVEL, "r"))) {
		snprintf (buffer, sizeof (buffer), "sysinit");
		return (buffer);
	}

	if (fgets (buffer, PATH_MAX, fp)) {
		int i = strlen (buffer) - 1;
		if (buffer[i] == '\n')
			buffer[i] = 0;
		fclose (fp);
		return (buffer);
	}

	fclose (fp);
	snprintf (buffer, sizeof (buffer), "sysinit");
	return (buffer);
}
librc_hidden_def(rc_get_runlevel)

void rc_set_runlevel (const char *runlevel)
{
	FILE *fp = fopen (SOFTLEVEL, "w");
	if (! fp)
		eerrorx ("failed to open `" SOFTLEVEL "': %s", strerror (errno));
	fprintf (fp, "%s", runlevel);
	fclose (fp);
}
librc_hidden_def(rc_set_runlevel)

bool rc_runlevel_exists (const char *runlevel)
{
	char *path;
	bool retval;

	if (! runlevel)
		return (false);

	path = rc_strcatpaths (RC_RUNLEVELDIR, runlevel, (char *) NULL);
	retval = rc_is_dir (path);
	free (path);
	return (retval);
}
librc_hidden_def(rc_runlevel_exists)

/* Resolve a service name to it's full path */
char *rc_resolve_service (const char *service)
{
	char buffer[PATH_MAX];
	char *file;
	int r = 0;

	if (! service)
		return (NULL);

	if (service[0] == '/')
		return (strdup (service));

	file = rc_strcatpaths (RC_SVCDIR, "started", service, (char *) NULL);
	if (! rc_is_link (file)) {
		free (file);
		file = rc_strcatpaths (RC_SVCDIR, "inactive", service, (char *) NULL);
		if (! rc_is_link (file)) {
			free (file);
			file = NULL;
		}
	}

	memset (buffer, 0, sizeof (buffer));
	if (file) {
		r = readlink (file, buffer, sizeof (buffer));
		free (file);
		if (r > 0)
			return (rc_xstrdup (buffer));
	}

	snprintf (buffer, sizeof (buffer), RC_INITDIR "/%s", service);
	return (strdup (buffer));
}
librc_hidden_def(rc_resolve_service)

bool rc_service_exists (const char *service)
{
	char *file;
	bool retval = false;
	int len;

	if (! service)
		return (false);

	len = strlen (service);

	/* .sh files are not init scripts */
	if (len > 2 && service[len - 3] == '.' &&
		service[len - 2] == 's' &&
		service[len - 1] == 'h')
		return (false);

	file = rc_resolve_service (service); 
	if (rc_exists (file))
		retval = rc_is_exec (file);
	free (file);
	return (retval);
}
librc_hidden_def(rc_service_exists)

char **rc_service_options (const char *service)
{
	char *svc;
	char cmd[PATH_MAX];
	char buffer[RC_LINEBUFFER];
	char **opts = NULL;
	char *token;
	char *p = buffer;
	FILE *fp;

	if (! rc_service_exists (service))
		return (NULL);

	svc = rc_resolve_service (service);

	snprintf (cmd, sizeof (cmd), ". '%s'; echo \"${opts}\"",  svc);
	if (! (fp = popen (cmd, "r"))) {
		eerror ("popen `%s': %s", svc, strerror (errno));
		free (svc);
		return (NULL);
	}

	if (fgets (buffer, RC_LINEBUFFER, fp)) {
		if (buffer[strlen (buffer) - 1] == '\n')
			buffer[strlen (buffer) - 1] = '\0';
		while ((token = strsep (&p, " ")))
			opts = rc_strlist_addsort (opts, token);
	}
	pclose (fp);
	return (opts);
}
librc_hidden_def(rc_service_options)

char *rc_service_description (const char *service, const char *option)
{
	char *svc;
	char cmd[PATH_MAX];
	char buffer[RC_LINEBUFFER];
	char *desc = NULL;
	FILE *fp;
	int i;

	if (! rc_service_exists (service))
		return (NULL);

	svc = rc_resolve_service (service);

	if (! option)
		option = "";

	snprintf (cmd, sizeof (cmd), ". '%s'; echo \"${description%s%s}\"",
			  svc, option ? "_" : "", option);
	if (! (fp = popen (cmd, "r"))) {
		eerror ("popen `%s': %s", svc, strerror (errno));
		free (svc);
		return (NULL);
	}
	free (svc);

	while (fgets (buffer, RC_LINEBUFFER, fp)) {
		if (! desc) {
			desc = rc_xmalloc (strlen (buffer) + 1);
			*desc = '\0';
		} else {
			desc = rc_xrealloc (desc, strlen (desc) + strlen (buffer) + 1);
		}
		i = strlen (desc);
		memcpy (desc + i, buffer, strlen (buffer));
		memset (desc + i + strlen (buffer), 0, 1);
	}

	pclose (fp);
	return (desc);
}
librc_hidden_def(rc_service_description)

bool rc_service_in_runlevel (const char *service, const char *runlevel)
{
	char *file;
	bool retval;
	char *svc;

	if (! runlevel || ! service)
		return (false);

	svc = rc_xstrdup (service);
	file = rc_strcatpaths (RC_RUNLEVELDIR, runlevel, basename (svc),
						   (char *) NULL);
	free (svc);
	retval = rc_exists (file);
	free (file);

	return (retval);
}
librc_hidden_def(rc_service_in_runlevel)

bool rc_mark_service (const char *service, const rc_service_state_t state)
{
	char *file;
	int i = 0;
	int skip_state = -1;
	char *base;
	char *svc;
	char *init = rc_resolve_service (service);
	bool skip_wasinactive = false;

	if (! service)
		return (false);

	svc = rc_xstrdup (service);
	base = basename (svc);

	if (state != rc_service_stopped) {
		if (! rc_is_file(init)) {
			free (init);
			free (svc);
			return (false);
		}

		file = rc_strcatpaths (RC_SVCDIR, rc_service_state_names[state], base,
							   (char *) NULL);
		if (rc_exists (file))
			unlink (file);
		i = symlink (init, file);
		if (i != 0)	{
			eerror ("symlink `%s' to `%s': %s", init, file, strerror (errno));
			free (file);
			free (init);
			free (svc);
			return (false);
		}

		free (file);
		skip_state = state;
	}

	if (state == rc_service_coldplugged) {
		free (init);
		free (svc);
		return (true);
	}

	/* Remove any old states now */
	i = 0;
	while (rc_service_state_names[i]) {
		if ((i != skip_state &&
			 i != rc_service_stopped &&
			 i != rc_service_coldplugged &&
			 i != rc_service_scheduled &&
			 i != rc_service_crashed) &&
			(! skip_wasinactive || i != rc_service_wasinactive))
		{
			file = rc_strcatpaths (RC_SVCDIR, rc_service_state_names[i], base,
								   (char *) NULL);
			if (rc_exists (file)) {
				if ((state == rc_service_starting ||
					 state == rc_service_stopping) &&
					i == rc_service_inactive)
				{
					char *wasfile = rc_strcatpaths (RC_SVCDIR,
													rc_service_state_names[rc_service_wasinactive],
													base, (char *) NULL);

					if (symlink (init, wasfile) != 0)
						eerror ("symlink `%s' to `%s': %s", init, wasfile,
								strerror (errno));

					skip_wasinactive = true;
					free (wasfile);
				}

				errno = 0;
				if (unlink (file) != 0 && errno != ENOENT)
					eerror ("failed to delete `%s': %s", file,
							strerror (errno));
			}
			free (file);
		}
		i++;
	}

	/* Remove the exclusive state if we're inactive */
	if (state == rc_service_started ||
		state == rc_service_stopped ||
		state == rc_service_inactive)
	{
		file = rc_strcatpaths (RC_SVCDIR, "exclusive", base, (char *) NULL);
		if (rc_exists (file))
			if (unlink (file) != 0)
				eerror ("unlink `%s': %s", file, strerror (errno));
		free (file);
	}

	/* Remove any options and daemons the service may have stored */
	if (state == rc_service_stopped) {
		char *dir = rc_strcatpaths (RC_SVCDIR, "options", base, (char *) NULL);

		if (rc_is_dir (dir))
			rc_rm_dir (dir, true);
		free (dir);

		dir = rc_strcatpaths (RC_SVCDIR, "daemons", base, (char *) NULL);
		if (rc_is_dir (dir))
			rc_rm_dir (dir, true);
		free (dir);

		rc_schedule_clear (service);
	}

	/* These are final states, so remove us from scheduled */
	if (state == rc_service_started || state == rc_service_stopped) {
		char *sdir = rc_strcatpaths (RC_SVCDIR, "scheduled", (char *) NULL);
		char **dirs = rc_ls_dir (NULL, sdir, 0);
		char *dir;
		int serrno;

		STRLIST_FOREACH (dirs, dir, i) {
			char *bdir = rc_strcatpaths (sdir, dir, (char *) NULL);
			file = rc_strcatpaths (bdir, base, (char *) NULL);
			if (rc_exists (file))
				if (unlink (file) != 0)
					eerror ("unlink `%s': %s", file, strerror (errno));
			free (file);

			/* Try and remove the dir - we don't care about errors */
			serrno = errno;
			rmdir (bdir);
			errno = serrno;
			free (bdir);
		}
		rc_strlist_free (dirs);
		free (sdir);
	}

	free (svc);
	free (init);
	return (true);
}
librc_hidden_def(rc_mark_service)

bool rc_service_state (const char *service, const rc_service_state_t state)
{
	char *file;
	bool retval;
	char *svc;

	/* If the init script does not exist then we are stopped */
	if (! rc_service_exists (service))
		return (state == rc_service_stopped ? true : false);

	/* We check stopped state by not being in any of the others */
	if (state == rc_service_stopped)
		return ( ! (rc_service_state (service, rc_service_started) ||
					rc_service_state (service, rc_service_starting) ||
					rc_service_state (service, rc_service_stopping) ||
					rc_service_state (service, rc_service_inactive)));

	/* The crashed state and scheduled states are virtual */
	if (state == rc_service_crashed)
		return (rc_service_daemons_crashed (service));
	else if (state == rc_service_scheduled) {
		char **services = rc_services_scheduled_by (service);
		retval = (services);
		if (services)
			free (services);
		return (retval);
	}

	/* Now we just check if a file by the service name rc_exists
	   in the state dir */
	svc = rc_xstrdup (service);
	file = rc_strcatpaths (RC_SVCDIR, rc_service_state_names[state],
						   basename (svc), (char*) NULL);
	free (svc);
	retval = rc_exists (file);
	free (file);
	return (retval);
}
librc_hidden_def(rc_service_state)

bool rc_get_service_option (const char *service, const char *option,
							char *value)
{
	FILE *fp;
	char buffer[RC_LINEBUFFER];
	char *file = rc_strcatpaths (RC_SVCDIR, "options", service, option,
								 (char *) NULL);
	bool retval = false;

	if (rc_exists (file)) {
		if ((fp = fopen (file, "r")) == NULL)
			eerror ("fopen `%s': %s", file, strerror (errno));
		else {
			memset (buffer, 0, sizeof (buffer));
			while (fgets (buffer, RC_LINEBUFFER, fp)) {
				memcpy (value, buffer, strlen (buffer));
				value += strlen (buffer);
			}
			fclose (fp);
			retval = true;
		}
	}

	free (file);
	return (retval);
}
librc_hidden_def(rc_get_service_option)

bool rc_set_service_option (const char *service, const char *option,
							const char *value)
{
	FILE *fp;
	char *path = rc_strcatpaths (RC_SVCDIR, "options", service, (char *) NULL);
	char *file = rc_strcatpaths (path, option, (char *) NULL);
	bool retval = false;

	if (! rc_is_dir (path)) {
		if (mkdir (path, 0755) != 0) {
			eerror ("mkdir `%s': %s", path, strerror (errno));
			free (path);
			free (file);
			return (false);
		}
	}

	if ((fp = fopen (file, "w")) == NULL)
		eerror ("fopen `%s': %s", file, strerror (errno));
	else {
		if (value)
			fprintf (fp, "%s", value);
		fclose (fp);
		retval = true;
	}

	free (path);
	free (file);
	return (retval);
}
librc_hidden_def(rc_set_service_option)

static pid_t _exec_service (const char *service, const char *arg)
{
	char *file;
	char *fifo;
	pid_t pid = -1;
	char *svc;

	file = rc_resolve_service (service);
	if (! rc_is_file (file)) {
		rc_mark_service (service, rc_service_stopped);
		free (file);
		return (0);
	}

	/* We create a fifo so that other services can wait until we complete */
	svc = rc_xstrdup (service);
	fifo = rc_strcatpaths (RC_SVCDIR, "exclusive", basename (svc),
						   (char *) NULL);
	free (svc);

	if (mkfifo (fifo, 0600) != 0 && errno != EEXIST) {
		eerror ("unable to create fifo `%s': %s", fifo, strerror (errno));
		free (fifo);
		free (file);
		return (-1);
	}

	if ((pid = vfork ()) == 0) {
		execl (file, file, arg, (char *) NULL);
		eerror ("unable to exec `%s': %s", file, strerror (errno));
		unlink (fifo);
		_exit (EXIT_FAILURE);
	}

	free (fifo);
	free (file);

	if (pid == -1)
		eerror ("vfork: %s", strerror (errno));
	
	return (pid);
}

int rc_waitpid (pid_t pid) {
	int status = 0;
	pid_t savedpid = pid;

	errno = 0;
	do {
		pid = waitpid (savedpid, &status, 0);
		if (pid < 0) {
			if (errno != ECHILD)
				eerror ("waitpid %d: %s", savedpid, strerror (errno));
			return (-1);
		}
	} while (! WIFEXITED (status) && ! WIFSIGNALED (status)); 
	
	return (WIFEXITED (status) ? WEXITSTATUS (status) : EXIT_FAILURE);
}

pid_t rc_stop_service (const char *service)
{
	if (rc_service_state (service, rc_service_stopped))
		return (0);

	return (_exec_service (service, "stop"));
}
librc_hidden_def(rc_stop_service)

pid_t rc_start_service (const char *service)
{
	if (! rc_service_state (service, rc_service_stopped))
		return (0);

	return (_exec_service (service, "start"));
}
librc_hidden_def(rc_start_service)

void rc_schedule_start_service (const char *service,
								const char *service_to_start)
{
	char *dir;
	char *init;
	char *file;
	char *svc;

	/* service may be a provided service, like net */
	if (! service || ! rc_service_exists (service_to_start))
		return;

	svc = rc_xstrdup (service);
	dir = rc_strcatpaths (RC_SVCDIR, "scheduled", basename (svc),
						  (char *) NULL);
	free (svc);
	if (! rc_is_dir (dir))
		if (mkdir (dir, 0755) != 0) {
			eerror ("mkdir `%s': %s", dir, strerror (errno));
			free (dir);
			return;
		}

	init = rc_resolve_service (service_to_start);
	svc = rc_xstrdup (service_to_start);
	file = rc_strcatpaths (dir, basename (svc), (char *) NULL);
	free (svc);
	if (! rc_exists (file) && symlink (init, file) != 0)
		eerror ("symlink `%s' to `%s': %s", init, file, strerror (errno));

	free (init);
	free (file);
	free (dir);
}
librc_hidden_def(rc_schedule_start_service)

void rc_schedule_clear (const char *service)
{
	char *svc  = rc_xstrdup (service);
	char *dir = rc_strcatpaths (RC_SVCDIR, "scheduled", basename (svc),
								(char *) NULL);

	free (svc);
	if (rc_is_dir (dir))
		rc_rm_dir (dir, true);
	free (dir);
}
librc_hidden_def(rc_schedule_clear)

bool rc_wait_service (const char *service)
{
	char *svc;
	char *base;
	char *fifo;
	struct timespec ts;
	int nloops = WAIT_MAX / WAIT_INTERVAL;
	bool retval = false;
	bool forever = false;

	if (! service)
		return (false);

	svc = rc_xstrdup (service);
	base = basename (svc);
	fifo = rc_strcatpaths (RC_SVCDIR, "exclusive", base, (char *) NULL);
	/* FIXME: find a better way of doing this
	 * Maybe a setting in the init script? */
	if (strcmp (base, "checkfs") == 0 || strcmp (base, "checkroot") == 0)
		forever = true;
	free (svc);

	ts.tv_sec = 0;
	ts.tv_nsec = WAIT_INTERVAL;

	while (nloops) {
		if (! rc_exists (fifo)) {
			retval = true;
			break;
		}

		if (nanosleep (&ts, NULL) == -1) {
			if (errno != EINTR) {
				eerror ("nanosleep: %s", strerror (errno));
				break;
			}
		}

		if (! forever)
			nloops --;
	}

	free (fifo);
	return (retval);
}
librc_hidden_def(rc_wait_service)

char **rc_services_in_runlevel (const char *runlevel)
{
	char *dir;
	char **list = NULL;

	if (! runlevel)
		return (rc_ls_dir (NULL, RC_INITDIR, RC_LS_INITD));

	/* These special levels never contain any services */
	if (strcmp (runlevel, RC_LEVEL_SYSINIT) == 0 ||
		strcmp (runlevel, RC_LEVEL_SINGLE) == 0)
		return (NULL);

	dir = rc_strcatpaths (RC_RUNLEVELDIR, runlevel, (char *) NULL);
	if (! rc_is_dir (dir))
		eerror ("runlevel `%s' does not exist", runlevel);
	else
		list = rc_ls_dir (list, dir, RC_LS_INITD);

	free (dir);
	return (list);
}
librc_hidden_def(rc_services_in_runlevel)

char **rc_services_in_state (rc_service_state_t state)
{
	char *dir = rc_strcatpaths (RC_SVCDIR, rc_service_state_names[state],
								(char *) NULL);
	char **list = NULL;

	if (state == rc_service_scheduled) {
		char **dirs = rc_ls_dir (NULL, dir, 0);
		char *d;
		int i;

		STRLIST_FOREACH (dirs, d, i) {
			char *p = rc_strcatpaths (dir, d, (char *) NULL);
			char **entries = rc_ls_dir (NULL, p, RC_LS_INITD);
			char *e;
			int j;

			STRLIST_FOREACH (entries, e, j)
				list = rc_strlist_addsortu (list, e);

			if (entries)
				free (entries);
		}

		if (dirs)
			free (dirs);
	} else {
		if (rc_is_dir (dir))
			list = rc_ls_dir (list, dir, RC_LS_INITD);
	}

	free (dir);
	return (list);
}
librc_hidden_def(rc_services_in_state)

bool rc_service_add (const char *runlevel, const char *service)
{
	bool retval;
	char *init;
	char *file;
	char *svc;

	if (! rc_runlevel_exists (runlevel)) {
		errno = ENOENT;
		return (false);
	}

	if (rc_service_in_runlevel (service, runlevel)) {
		errno = EEXIST;
		return (false);
	}

	init = rc_resolve_service (service);
	svc = rc_xstrdup (service);
	file = rc_strcatpaths (RC_RUNLEVELDIR, runlevel, basename (svc),
						   (char *) NULL);
	free (svc);
	retval = (symlink (init, file) == 0);
	free (init);
	free (file);
	return (retval);
}
librc_hidden_def(rc_service_add)

bool rc_service_delete (const char *runlevel, const char *service)
{
	char *file;
	char *svc;
	bool retval = false;

	if (! runlevel || ! service)
		return (false);

	svc = rc_xstrdup (service);
	file = rc_strcatpaths (RC_RUNLEVELDIR, runlevel, basename (svc),
						   (char *) NULL);
	free (svc);
	if (unlink (file) == 0)
		retval = true;

	free (file);
	return (retval);
}
librc_hidden_def(rc_service_delete)

char **rc_services_scheduled_by (const char *service)
{
	char **dirs = rc_ls_dir (NULL, RC_SVCDIR "/scheduled", 0);
	char **list = NULL;
	char *dir;
	int i;

	STRLIST_FOREACH (dirs, dir, i) {
		char *file = rc_strcatpaths (RC_SVCDIR, "scheduled", dir, service,
									 (char *) NULL);
		if (rc_exists (file))
			list = rc_strlist_add (list, file);
		free (file);
	}
	rc_strlist_free (dirs);

	return (list);
}
librc_hidden_def(rc_services_scheduled_by)

char **rc_services_scheduled (const char *service)
{
	char *svc = rc_xstrdup (service);
	char *dir = rc_strcatpaths (RC_SVCDIR, "scheduled", basename (svc),
								(char *) NULL);
	char **list = NULL;

	if (rc_is_dir (dir))
		list = rc_ls_dir (list, dir, RC_LS_INITD);

	free (svc);
	free (dir);
	return (list);
}
librc_hidden_def(rc_services_scheduled)

bool rc_allow_plug (char *service)
{
	char *list;
	char *p;
	char *star;
	char *token;
	bool allow = true;
	char *match = getenv ("RC_PLUG_SERVICES");
	if (! match)
		return true;

	list = rc_xstrdup (match);
	p = list;
	while ((token = strsep (&p, " "))) {
		bool truefalse = true;
		if (token[0] == '!') {
			truefalse = false;
			token++;
		}

		star = strchr (token, '*');
		if (star) {
			if (strncmp (service, token, star - token) == 0) {
				allow = truefalse;
				break;
			}
		} else {
			if (strcmp (service, token) == 0) {
				allow = truefalse;
				break;
			}
		}
	}

	free (list);
	return (allow);
}
librc_hidden_def(rc_allow_plug)

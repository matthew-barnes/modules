/*************************************************************************
 *
 * ENVMODULES.C, Modules Tcl extension library
 * Copyright (C) 2018-2020 Xavier Delaruelle
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 ************************************************************************/

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "envmodules.h"


/*	Utility function to compare 2 integers for qsort function. */
int
__Envmodules_IntCmp(
   const void* i,
   const void* j)
{
   return (i > j) - (i < j);
}

/*----------------------------------------------------------------------
 *
 * Envmodules_GetFilesInDirectoryObjCmd --
 *
 *	 This function is invoked to read the content of a directory in a more
 *	 IO-optimized way than native Tcl commands perform by avoiding specific
 *	 additional queries to get hidden files like .modulerc and .version.
 *
 * Results:
 *	 A standard Tcl result.
 *
 * Side effects:
 *	 None.
 *
 *---------------------------------------------------------------------*/

int
Envmodules_GetFilesInDirectoryObjCmd(
   ClientData dummy,       /* Not used. */
   Tcl_Interp *interp,     /* Current interpreter. */
   int objc,               /* Number of arguments. */
   Tcl_Obj *const objv[])  /* Argument objects. */
{
   int fetch_dotversion;
   const char *dir;
   int dirlen;
   DIR *did;
   Tcl_Obj *ltmp, *lres;
   struct dirent *direntry;
   int have_modulerc = 0;
   int have_version = 0;
   int is_hidden;
   char path[PATH_MAX];

   /* Parse arguments. */
   if (objc == 3) {
      /* fetch_dotversion */
      if (Tcl_GetBooleanFromObj(interp, objv[2], &fetch_dotversion)!=TCL_OK) {
         Tcl_SetErrorCode(interp, "TCL", "VALUE", "BOOLEAN", NULL);
         return TCL_ERROR;
      }
   } else {
      Tcl_WrongNumArgs(interp, 1, objv, "dir fetch_dotversion");
      return TCL_ERROR;
   }

   dir = Tcl_GetStringFromObj(objv[1], &dirlen);

   /* Open directory. */
   if ((did  = opendir(dir)) == NULL) {
      Tcl_SetErrno(errno);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
      Tcl_AppendResult(interp, "couldn't open directory \"", dir, "\": ",
         Tcl_PosixError(interp), (char *) NULL);
#else
      Tcl_SetObjResult(interp, Tcl_ObjPrintf(
         "couldn't open directory \"%s\": %s", dir, Tcl_PosixError(interp)));
#endif
      return TCL_ERROR;
   }

   /* Read directory. */
   ltmp = Tcl_NewListObj(0, NULL);
   Tcl_IncrRefCount(ltmp);
   errno = 0;
   while ((direntry = readdir(did)) != NULL) {
      snprintf(path, sizeof(path), "%s/%s", dir, direntry->d_name);
      /* ignore . and .. */
      if (!strcmp(direntry->d_name, ".") || !strcmp(direntry->d_name, "..")) {
         continue;
      } else if (!strcmp(direntry->d_name, ".modulerc")) {
         if (!access(path, R_OK)) {
            have_modulerc = 1;
         }
      } else if (!strcmp(direntry->d_name, ".version")) {
         if (fetch_dotversion && !access(path, R_OK)) {
            have_version = 1;
         }
      } else {
         Tcl_ListObjAppendElement(interp, ltmp, Tcl_NewStringObj(path, -1));
         is_hidden = (direntry->d_name[0] == '.') ? 1 : 0;
         Tcl_ListObjAppendElement(interp, ltmp, Tcl_NewIntObj(is_hidden));
      }
   }
   /* Do not treat error happening during read to send list of valid files. */

   /* Close directory. */
   if (closedir(did) == -1) {
      Tcl_SetErrno(errno);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
      Tcl_AppendResult(interp, "couldn't close directory \"", dir, "\": ",
         Tcl_PosixError(interp), (char *) NULL);
#else
      Tcl_SetObjResult(interp, Tcl_ObjPrintf(
         "couldn't close directory \"%s\": %s", dir, Tcl_PosixError(interp)));
#endif
      Tcl_DecrRefCount(ltmp);
      return TCL_ERROR;
   }

   /* Build result list. */
   lres = Tcl_NewObj();
   Tcl_IncrRefCount(lres);
   /* Ensure .modulerc and .version are first entries in result list */
   if (have_modulerc) {
      snprintf(path, sizeof(path), "%s/%s", dir, ".modulerc");
      Tcl_ListObjAppendElement(interp, lres, Tcl_NewStringObj(path, -1));
      Tcl_ListObjAppendElement(interp, lres, Tcl_NewIntObj(0));
   }
   if (have_version) {
      snprintf(path, sizeof(path), "%s/%s", dir, ".version");
      Tcl_ListObjAppendElement(interp, lres, Tcl_NewStringObj(path, -1));
      Tcl_ListObjAppendElement(interp, lres, Tcl_NewIntObj(0));
   }
   /* Then append regular elements. */
   Tcl_ListObjAppendList(interp, lres, ltmp);
   Tcl_DecrRefCount(ltmp);


   Tcl_SetObjResult(interp, lres);
   Tcl_DecrRefCount(lres);
   return TCL_OK;
}

/*----------------------------------------------------------------------
 *
 * Envmodules_ReadFileObjCmd --
 *
 *	 This function is invoked to open/read/close a regular file in a
 *	 more IO-optimized way than native Tcl commands perform by avoiding
 *	 useless lstat, fcntl and ioctl syscalls.
 *
 * Results:
 *	 A standard Tcl result.
 *
 * Side effects:
 *	 None.
 *
 *---------------------------------------------------------------------*/

int
Envmodules_ReadFileObjCmd(
   ClientData dummy,       /* Not used. */
   Tcl_Interp *interp,     /* Current interpreter. */
   int objc,               /* Number of arguments. */
   Tcl_Obj *const objv[])  /* Argument objects. */
{
   int firstline;
   const char *filename;
   int filenamelen;
   int fid;
   ssize_t len;
   char buf[READ_BUFFER_SIZE];
   Tcl_Obj *res;

   /* Parse arguments. */
   if (objc == 2) {
      firstline = 0;
   } else if (objc == 3) {
      if (Tcl_GetBooleanFromObj(interp, objv[2], &firstline) != TCL_OK) {
         Tcl_SetErrorCode(interp, "TCL", "VALUE", "BOOLEAN", NULL);
         return TCL_ERROR;
      }
   } else {
      Tcl_WrongNumArgs(interp, 1, objv, "filename ?firstline?");
      return TCL_ERROR;
   }

   filename = Tcl_GetStringFromObj(objv[1], &filenamelen);

   /* Open file. */
   if ((fid  = open(filename, O_RDONLY)) == -1) {
      Tcl_SetErrno(errno);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
      Tcl_AppendResult(interp, "couldn't open \"", filename, "\": ",
         Tcl_PosixError(interp), (char *) NULL);
#else
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("couldn't open \"%s\": %s",
         filename, Tcl_PosixError(interp)));
#endif
      return TCL_ERROR;
   }

   /* Read file. */
   res = Tcl_NewObj();
   Tcl_IncrRefCount(res);
   /* Only read first characters to get magic cookie. */
   if (firstline == 1) {
      if ((len = read(fid, buf, FIRSTLINE_LENGTH)) > 0) {
         Tcl_AppendToObj(res, buf, len);
      }
   } else {
      while ((len = read(fid, buf, READ_BUFFER_SIZE)) > 0) {
         Tcl_AppendToObj(res, buf, len);
      }
   }
   /* Error during read. */
   if (len == -1) {
      Tcl_SetErrno(errno);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
      Tcl_AppendResult(interp, "error reading \"", filename, "\": ",
         Tcl_PosixError(interp), (char *) NULL);
#else
      Tcl_SetObjResult(interp, Tcl_ObjPrintf("error reading \"%s\": %s",
         filename, Tcl_PosixError(interp)));
#endif
      Tcl_DecrRefCount(res);
      close(fid);
      return TCL_ERROR;
   }

   /* Close file. */
   close(fid);

   Tcl_SetObjResult(interp, res);
   Tcl_DecrRefCount(res);
   return TCL_OK;
}

/*----------------------------------------------------------------------
 *
 * Envmodules_InitStateUsernameObjCmd --
 *
 *	 This function is invoked to return the username of user running
 *	 current process.
 *
 * Results:
 *	 A standard Tcl result.
 *
 * Side effects:
 *	 None.
 *
 *---------------------------------------------------------------------*/

int
Envmodules_InitStateUsernameObjCmd(
   ClientData dummy,       /* Not used. */
   Tcl_Interp *interp,     /* Current interpreter. */
   int objc,               /* Number of arguments. */
   Tcl_Obj *const objv[])  /* Argument objects. */
{
   uid_t uid;
   struct passwd *pwd;
   char uidstr[16];
   Tcl_Obj *res;

   /* Get current user id */
   uid = getuid();

   /* Fetch corresponding passwd entry */
   if ((pwd = getpwuid(uid)) == NULL) {
      Tcl_SetErrno(errno);
      sprintf (uidstr, "%d", uid);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
      Tcl_AppendResult(interp, "couldn't find name for user id \"", uidstr,
         "\": ", Tcl_PosixError(interp), (char *) NULL);
#else
      Tcl_SetObjResult(interp,
         Tcl_ObjPrintf("couldn't find name for user id \"%s\": %s", uidstr,
         Tcl_PosixError(interp)));
#endif
      return TCL_ERROR;
   }

   /* Set username as result */
   res = Tcl_NewObj();
   Tcl_IncrRefCount(res);
   Tcl_AppendToObj(res, pwd->pw_name, strlen(pwd->pw_name));

   Tcl_SetObjResult(interp, res);
   Tcl_DecrRefCount(res);
   return TCL_OK;
}

/*----------------------------------------------------------------------
 *
 * Envmodules_InitStateUsergroupsObjCmd --
 *
 *	 This function is invoked to return all the groups the user running
 *	 current process is member of.
 *
 * Results:
 *	 A standard Tcl result.
 *
 * Side effects:
 *	 None.
 *
 *---------------------------------------------------------------------*/

int
Envmodules_InitStateUsergroupsObjCmd(
   ClientData dummy,       /* Not used. */
   Tcl_Interp *interp,     /* Current interpreter. */
   int objc,               /* Number of arguments. */
   Tcl_Obj *const objv[])  /* Argument objects. */
{
   int maxgroups;
   GETGROUPS_T *groups;
   int ngroups = 0;
   int egid_in_groups = 0;
   GETGROUPS_T egid;
   int i, j;
   struct group *grp;
   char gidstr[16];
   Tcl_Obj *lres;

   /* Get actually configured number of groups */
#if defined(HAVE_SYSCONF) && defined(_SC_NGROUPS_MAX)
   maxgroups = sysconf(_SC_NGROUPS_MAX);
#else
#  if defined(NGROUPS_MAX)
   maxgroups = NGROUPS_MAX;
#  else
   maxgroups = DEFAULT_MAXGROUPS;
#  endif
#endif

   /* Fetch supplementary group list unless getgroups not supported */
   groups = (GETGROUPS_T *) ckalloc(maxgroups * sizeof(GETGROUPS_T));

#if defined (HAVE_GETGROUPS)
   if ((ngroups = getgroups(maxgroups, groups)) == -1) {
      Tcl_SetErrno(errno);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
      Tcl_AppendResult(interp, "couldn't get supplementary groups: ",
         Tcl_PosixError(interp), (char *) NULL);
#else
      Tcl_SetObjResult(interp,
         Tcl_ObjPrintf("couldn't get supplementary groups: %s",
         Tcl_PosixError(interp)));
#endif
      ckfree((char *) groups);
      return TCL_ERROR;
   }
#endif

   /* Sort then remove duplicates from getgroups result */
   if (ngroups > 1) {
      qsort(groups, ngroups, sizeof(GETGROUPS_T), __Envmodules_IntCmp);
      j = 0;
      for (i = 1; i < ngroups; i++) {
         if (groups[i] != groups[j]) {
            j++;
            groups[j] = groups[i];
         }
      }
      ngroups = j + 1;
   }

   /* Add primary group if not part of getgroups result (or if getgroups
    * function is not available) */
   egid = getegid();
   for (i = 0; i < ngroups; i++) {
      if (egid == groups[i]) {
         egid_in_groups = 1;
         break;
      }
   }
   if (egid_in_groups == 0) {
      groups[ngroups] = egid;
      ngroups++;
   }

   /* Add group name of primary gid and each supplementatry gid to result
    * list */
   lres = Tcl_NewObj();
   Tcl_IncrRefCount(lres);
   for (i = 0; i < ngroups; i++) {
      if ((grp = getgrgid(groups[i])) == NULL) {
         Tcl_SetErrno(errno);
         sprintf(gidstr, "%d", groups[i]);
#if TCL_MAJOR_VERSION == 8 && TCL_MINOR_VERSION < 5
         Tcl_AppendResult(interp, "couldn't find name for group id \"",
            gidstr, "\": ", Tcl_PosixError(interp), (char *) NULL);
#else
         Tcl_SetObjResult(interp,
            Tcl_ObjPrintf("couldn't find name for group id \"%s\": %s",
            gidstr, Tcl_PosixError(interp)));
#endif
         ckfree((char *) groups);
         return TCL_ERROR;
      }
      Tcl_ListObjAppendElement(interp, lres, Tcl_NewStringObj(grp->gr_name,
         -1));
   }

   Tcl_SetObjResult(interp, lres);
   Tcl_DecrRefCount(lres);
   ckfree((char *) groups);
   return TCL_OK;
}

/*----------------------------------------------------------------------
 *
 * Envmodules_Init --
 *
 *  Initialize the Modules commands.
 *
 * Results:
 *  TCL_OK if the package was properly initialized.
 *
 * Side effects:
 *  Adds package commands to the current interp.
 *
 *---------------------------------------------------------------------*/

DLLEXPORT int
Envmodules_Init(
   Tcl_Interp* interp      /* Tcl interpreter */
) {
    /* Require Tcl */
   if (Tcl_InitStubs(interp, "8.4", 0) == NULL) {
      return TCL_ERROR;
   }

   /* Create the provided commands */
   Tcl_CreateObjCommand(interp, "readFile", Envmodules_ReadFileObjCmd,
      (ClientData) NULL, (Tcl_CmdDeleteProc*) NULL);
   Tcl_CreateObjCommand(interp, "getFilesInDirectory",
      Envmodules_GetFilesInDirectoryObjCmd, (ClientData) NULL,
      (Tcl_CmdDeleteProc*) NULL);
   Tcl_CreateObjCommand(interp, "initStateUsername",
      Envmodules_InitStateUsernameObjCmd, (ClientData) NULL,
      (Tcl_CmdDeleteProc*) NULL);
   Tcl_CreateObjCommand(interp, "initStateUsergroups",
      Envmodules_InitStateUsergroupsObjCmd, (ClientData) NULL,
      (Tcl_CmdDeleteProc*) NULL);

   /* Provide the Envmodules package */
   return Tcl_PkgProvide(interp, PACKAGE_NAME, PACKAGE_VERSION);
}

/* vim:set tabstop=3 shiftwidth=3 expandtab autoindent: */

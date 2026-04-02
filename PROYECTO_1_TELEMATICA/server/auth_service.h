/*
 * auth_service.h
 * --------------
 * External authentication service for the IoT monitoring server.
 *
 * Required by project spec:
 *   "El sistema no debe almacenar usuarios localmente en el servidor
 *    principal. Cuando un usuario intente iniciar sesión, el servidor
 *    debe consultar un servicio externo de identidad."
 *
 * Implementation: the auth service reads credentials from an external
 * file (simulating a remote identity service).  The file path is
 * configurable and can be mounted via Docker volume or fetched from
 * a remote location.
 *
 * File format (users.conf):
 *   username:password:role
 *   engineer:eng2025:ENGINEER
 *   operator1:op123:SUPERVISOR
 *
 * In production this would be replaced by LDAP, OAuth, or an HTTP
 * call to an identity provider.
 */

#ifndef AUTH_SERVICE_H
#define AUTH_SERVICE_H

/* Roles returned by the auth service */
#define AUTH_ROLE_NONE        -1
#define AUTH_ROLE_SUPERVISOR   0
#define AUTH_ROLE_ENGINEER     1

/*  Initialise the auth service with the path to the credentials file.
 *  Returns 0 on success, -1 if the file cannot be opened.
 *  If filepath is NULL, uses a built-in default path "users.conf". */
int   auth_service_init(const char *filepath);

/*  Validate credentials against the external identity store.
 *
 *  Returns:
 *    AUTH_ROLE_ENGINEER    if valid engineer credentials
 *    AUTH_ROLE_SUPERVISOR  if valid supervisor credentials
 *    AUTH_ROLE_NONE (-1)   if invalid credentials
 *
 *  The file is re-read on every call so that changes to the
 *  identity store take effect without restarting the server. */
int   auth_service_validate(const char *username, const char *password);

/*  Clean up resources. */
void  auth_service_destroy(void);

#endif /* AUTH_SERVICE_H */
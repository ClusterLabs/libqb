/**
 * @mainpage
 *
 * @section overview Overview
 *
 * libqb is a library with the primary purpose of providing high-performance,
 * reusable features for client-server architecture, such as logging, tracing,
 * inter-process communication (IPC), and polling.  Except for some documented
 * anti-pattern use cases regarding IPC communication and logging, it is deemed
 * thread-safe.
 *
 * We don't intend this to be an all-encompassing library, but instead
 * provide very specially focused APIs that are highly tuned for maximum
 * performance for client/server applications.
 *
 * See the following pages for more info:
 * - @subpage qb_list_overview
 * - @subpage qb_atomic_overview
 * - @subpage qb_array_overview
 * - @subpage qb_map_overview
 * - @subpage qb_hdb_overview
 * - @subpage qb_rb_overview
 * - @subpage qb_loop_overview
 * - @subpage qb_log_overview
 * - @subpage qb_ipc_overview
 * - @subpage qb_util_overview
 */

/**
 * @page qb_rb_overview Ringbuffer
 * @copydoc qbrb.h
 * @see qbrb.h
 */

/**
 * @page qb_list_overview List
 * @copydoc qblist.h
 * @see qblist.h
 */

/**
 * @page qb_array_overview Array
 * @copydoc qbarray.h
 * @see qbarray.h
 */

/**
 * @page qb_map_overview Map
 * @copydoc qbmap.h
 * @see qbmap.h
 */

/** 
 * @page qb_hdb_overview Handle Database
 * @copydoc qbhdb.h
 * @see qbhdb.h
 */

/**
 * @page qb_loop_overview Main Loop
 * @copydoc qbloop.h
 * @see qbloop.h
 */

/**
 * @page qb_log_overview Logging
 * @copydoc qblog.h
 * @see qblog.h
 */

/**
 * @page qb_ipc_overview IPC Overview
 *
 * @par Overview
 * libqb provides a generically reusable very high performance shared memory IPC system for client
 * and service applications.  It supports many features including:
 * - Multiple transport implementations
 *   -# Shared memory implementation for very high performance.
 *   -# Unix sockets
 * - A synchronous request/response channel and asynchronous response channel per ipc connection.
 * - User defined private data per IPC connection.
 * - Ability to call a function per service on ipc connection and disconnection.
 * - Authenticated IPC connection with ability for developer to define which UIDs and GIDs are valid at connection time.
 * - Fully abstracted poll system so that any poll library may be used.
 * - User defined selector for determining the proper function to call per service and id.
 *
 * @par Security
 * The ipc system uses default operating system security mechanics to ensure ipc 
 * connections are validated.  A callback used with qb_ipcs_create() is called
 * for every new ipc connection with the parameters of UID and GID.  The callback
 * then determines if the UID and GID are authenticated for communication.
 *
 * @par Performance
 * For performance, QB_IPC_SHM (shared memory) is recommended. It is tuned for
 * very high performance.
 *
 * @par Multithreading
 * As of current implementation, there are not many guarantees about ipc system
 * being thread-safe.  What remains there is mostly owing to the encapsulation
 * of independent IPC connections.  Therefore it is highly recommended to have
 * a single one pushed throughout its lifecycle just with a single thread;
 * anything else would likely warrant external synchronization enforcement.
 *
 * @par Client API
 * @copydoc qbipcc.h
 * @see qbipcc.h
 *
 * @par Server API
 * @copydoc qbipcs.h
 * @see qbipcs.h
 *
 */

/**
 * @page qb_atomic_overview Atomic operations
 * @copydoc qbatomic.h
 * @see qbatomic.h
 */

/**
 * @page qb_util_overview Common Utilities
 * @copydoc qbutil.h
 * @see qbutil.h
 */

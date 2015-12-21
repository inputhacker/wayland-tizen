#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wayland-server.h"
#include "test-runner.h"


#define wl_client_for_each(client, list)     \
   for (client = 0, client = wl_client_from_link((list)->next);   \
        wl_client_get_link(client) != (list);                     \
        client = wl_client_from_link(wl_client_get_link(client)->next))

TEST(query_clients_tst)
{
   struct wl_display *display;
   struct wl_client *client, *client2;
   struct wl_list * client_list;
   int s[2];
   int find_client = 0;

   assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s) == 0);
   display = wl_display_create();
   assert(display);
   client = wl_client_create(display, s[0]);
   assert(client);

   client_list = wl_display_get_client_list(display);

   wl_client_for_each(client2, client_list)
     {
        if (client == client2)
           find_client = 1;
     }
   assert(find_client);

   wl_client_destroy(client);

   close(s[0]);
   close(s[1]);

   wl_display_destroy(display);
}

static unsigned res_id = 0;
static void resource_cb_func(void *element, void *data)
{
   struct wl_resource *resource = element;
   int *find_resource = data;
   int id = 0;

   id = wl_resource_get_id(resource);
   if (id == res_id) *find_resource = 1;
}

TEST(query_clients_resource_tst)
{
   struct wl_display *display;
   struct wl_client *client, *client2;
   struct wl_list * client_list;
   struct wl_resource *res;
   struct wl_map *res_objs;
   int s[2];
   int find_resource = 0;
   int find_client = 0;

   assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s) == 0);
   display = wl_display_create();
   assert(display);
   client = wl_client_create(display, s[0]);
   assert(client);
   res = wl_resource_create(client, &wl_display_interface, 4, 0);
   assert(res);
   res_id = wl_resource_get_id(res);
   assert(res_id);

   client_list = wl_display_get_client_list(display);

   wl_client_for_each(client2, client_list)
     {
        if (client == client2)
          {
             find_client = 1;
             res_objs = wl_client_get_resources(client);
             wl_map_for_each(res_objs, resource_cb_func, &find_resource);
             assert(find_resource);
          }
     }
   assert(find_client);

   wl_client_destroy(client);

   close(s[0]);
   close(s[1]);

   wl_display_destroy(display);
   return;
}

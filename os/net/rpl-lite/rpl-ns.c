/*
 * Copyright (c) 2016, Inria.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 */

/**
 * \addtogroup rpl-lite
 * @{
 *
 * \file
 *         RPL non-storing mode specific functions. Includes support for
 *         source routing.
 *
 * \author Simon Duquennoy <simon.duquennoy@inria.fr>
 */

#include "net/rpl-lite/rpl.h"
#include "lib/list.h"
#include "lib/memb.h"

/* Log configuration */
#include "sys/log.h"
#define LOG_MODULE "RPL"
#define LOG_LEVEL LOG_LEVEL_RPL

/* Total number of nodes */
static int num_nodes;

/* Every known node in the network */
LIST(nodelist);
MEMB(nodememb, rpl_ns_node_t, RPL_NS_LINK_NUM);

/*---------------------------------------------------------------------------*/
int
rpl_ns_num_nodes(void)
{
  return num_nodes;
}
/*---------------------------------------------------------------------------*/
static int
node_matches_address(const rpl_ns_node_t *node, const uip_ipaddr_t *addr)
{
  return addr != NULL
      && node != NULL
      && !memcmp(addr, &curr_instance.dag.dag_id, 8)
      && !memcmp(((const unsigned char *)addr) + 8, node->link_identifier, 8);
}
/*---------------------------------------------------------------------------*/
rpl_ns_node_t *
rpl_ns_get_node(const uip_ipaddr_t *addr)
{
  rpl_ns_node_t *l;
  for(l = list_head(nodelist); l != NULL; l = list_item_next(l)) {
    /* Compare prefix and node identifier */
    if(node_matches_address(l, addr)) {
      return l;
    }
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
int
rpl_ns_is_addr_reachable(const uip_ipaddr_t *addr)
{
  int max_depth = RPL_NS_LINK_NUM;
  rpl_ns_node_t *node = rpl_ns_get_node(addr);
  rpl_ns_node_t *root_node = rpl_ns_get_node(&curr_instance.dag.dag_id);
  while(node != NULL && node != root_node && max_depth > 0) {
    node = node->parent;
    max_depth--;
  }
  return node != NULL && node == root_node;
}
/*---------------------------------------------------------------------------*/
void
rpl_ns_expire_parent(const uip_ipaddr_t *child, const uip_ipaddr_t *parent)
{
  rpl_ns_node_t *l = rpl_ns_get_node(child);
  /* Check if parent matches */
  if(l != NULL && node_matches_address(l->parent, parent)) {
    l->lifetime = RPL_NOPATH_REMOVAL_DELAY;
  }
}
/*---------------------------------------------------------------------------*/
rpl_ns_node_t *
rpl_ns_update_node(const uip_ipaddr_t *child, const uip_ipaddr_t *parent, uint32_t lifetime)
{
  rpl_ns_node_t *child_node = rpl_ns_get_node(child);
  rpl_ns_node_t *parent_node = rpl_ns_get_node(parent);
  rpl_ns_node_t *old_parent_node;

  if(parent != NULL) {
    /* No node for the parent, add one with infinite lifetime */
    if(parent_node == NULL) {
      parent_node = rpl_ns_update_node(parent, NULL, RPL_ROUTE_INFINITE_LIFETIME);
      if(parent_node == NULL) {
        LOG_ERR("NS: no space left for root node!\n");
        return NULL;
      }
    }
  }

  /* No node for this child, add one */
  if(child_node == NULL) {
    child_node = memb_alloc(&nodememb);
    /* No space left, abort */
    if(child_node == NULL) {
      LOG_ERR("NS: no space left for child ");
      LOG_ERR_6ADDR(child);
      LOG_ERR_("\n");
      return NULL;
    }
    child_node->parent = NULL;
    list_add(nodelist, child_node);
    num_nodes++;
  }

  /* Initialize node */
  child_node->lifetime = lifetime;
  memcpy(child_node->link_identifier, ((const unsigned char *)child) + 8, 8);

  /* Is the node reachable before the update? */
  if(rpl_ns_is_addr_reachable(child)) {
    old_parent_node = child_node->parent;
    /* Update node */
    child_node->parent = parent_node;
    /* Has the node become unreachable? May happen if we create a loop. */
    if(!rpl_ns_is_addr_reachable(child)) {
      /* The new parent makes the node unreachable, restore old parent.
       * We will take the update next time, with chances we know more of
       * the topology and the loop is gone. */
      child_node->parent = old_parent_node;
    }
  } else {
    child_node->parent = parent_node;
  }

  LOG_INFO("NS: updating link, child ");
  LOG_INFO_6ADDR(child);
  LOG_INFO_(", parent ");
  LOG_INFO_6ADDR(parent);
  LOG_INFO_(", lifetime %u, num_nodes %u\n", (unsigned)lifetime, num_nodes);

  return child_node;
}
/*---------------------------------------------------------------------------*/
void
rpl_ns_init(void)
{
  num_nodes = 0;
  memb_init(&nodememb);
  list_init(nodelist);
}
/*---------------------------------------------------------------------------*/
rpl_ns_node_t *
rpl_ns_node_head(void)
{
  return list_head(nodelist);
}
/*---------------------------------------------------------------------------*/
rpl_ns_node_t *
rpl_ns_node_next(rpl_ns_node_t *item)
{
  return list_item_next(item);
}
/*---------------------------------------------------------------------------*/
int
rpl_ns_get_node_global_addr(uip_ipaddr_t *addr, rpl_ns_node_t *node)
{
  if(addr != NULL && node != NULL) {
    memcpy(addr, &curr_instance.dag.dag_id, 8);
    memcpy(((unsigned char *)addr) + 8, &node->link_identifier, 8);
    return 1;
  } else {
    return 0;
  }
}
/*---------------------------------------------------------------------------*/
void
rpl_ns_periodic(unsigned seconds)
{
  rpl_ns_node_t *l;
  rpl_ns_node_t *next;
  /* First pass, decrement lifetime for all nodes with non-infinite lifetime */
  for(l = list_head(nodelist); l != NULL; l = list_item_next(l)) {
    /* Don't touch infinite lifetime nodes */
    if(l->lifetime != RPL_ROUTE_INFINITE_LIFETIME) {
      l->lifetime = l->lifetime > seconds ? l->lifetime - seconds : 0;
    }
  }
  /* Second pass, for all expired nodes, deallocate them iff no child points to them */
  for(l = list_head(nodelist); l != NULL; l = next) {
    next = list_item_next(l);
    if(l->lifetime == 0) {
      rpl_ns_node_t *l2;
      for(l2 = list_head(nodelist); l2 != NULL; l2 = list_item_next(l2)) {
        if(l2->parent == l) {
          break;
        }
      }
#if LOG_INFO_ENABLED
      uip_ipaddr_t node_addr;
      rpl_ns_get_node_global_addr(&node_addr, l);
      LOG_INFO("NS: removing expired node ");
      LOG_INFO_6ADDR(&node_addr);
      LOG_INFO_("\n");
#endif /* LOG_INFO_ENABLED */
      /* No child found, deallocate node */
      list_remove(nodelist, l);
      memb_free(&nodememb, l);
      num_nodes--;
    }
  }
}
/*---------------------------------------------------------------------------*/
void
rpl_ns_free_all(void)
{
  rpl_ns_node_t *l;
  rpl_ns_node_t *next;
  for(l = list_head(nodelist); l != NULL; l = next) {
    next = list_item_next(l);
    list_remove(nodelist, l);
    memb_free(&nodememb, l);
    num_nodes--;
  }
}
/** @} */

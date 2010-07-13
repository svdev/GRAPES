/*
 *  Copyright (c) 2009 Luca Abeni
 *
 *  This is free software; see gpl-3.0.txt
 */
#include <sys/select.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "net_helper.h"
#include "net_helpers.h"
#include "chunk.h"
#include "trade_msg_la.h"
#include "trade_msg_ha.h"
#include "chunkidset.h"
#include "msg_types.h"
#include "trade_sig_ha.h"
#include "chunkid_set_h.h"

#define BUFFSIZE 1024

static const char *my_addr = "127.0.0.1";
static int port = 6666;
static int dst_port;
static const char *dst_ip;
static int random_bmap = 0;
enum sigz { offer, request, sendbmap, reqbmap, unknown
};
static enum sigz sig = unknown;
static struct chunkID_set *cset = NULL;
static struct chunkID_set *rcset = NULL;
static struct nodeID *remote;
static uint8_t buff[BUFFSIZE];
static int max_deliver, trans_id, ret, chunktosend;
static enum signaling_type sig_type;
static struct nodeID *owner;


static struct nodeID *init(void)
{
  struct nodeID *myID;

  myID = net_helper_init(my_addr, port);
  if (myID == NULL) {
    fprintf(stderr, "Error creating my socket (%s:%d)!\n", my_addr, port);

    return NULL;
  }  
  chunkSignalingInit(myID);
  return myID;
}

static void cmdline_parse(int argc, char *argv[])
{
  int o;

  while ((o = getopt(argc, argv, "p:i:P:I:ORBbN")) != -1) {
    switch(o) {
      case 'p':
        dst_port = atoi(optarg);
        break;
      case 'i':
        dst_ip = strdup(optarg);
        break;
      case 'P':
        port =  atoi(optarg);
        break;
      case 'I':
        my_addr = iface_addr(optarg);
        break;
      case 'O':
          sig = offer;
          break;
      case 'R':
          sig = request;
          break;
      case 'B':
          sig = sendbmap;
          break;
      case 'b':
          sig = reqbmap;
          break;
     case 'N':
          random_bmap = 1;
          break;
     default:
        fprintf(stderr, "Error: unknown option %c\n", o);

        exit(-1);
    }
  }
}


int client_side(struct nodeID *my_sock)
{
    struct nodeID *dst;

    max_deliver = trans_id = ret = 0;
    dst = create_node(dst_ip, dst_port);
    fprintf(stdout,"Sending signal ");
    switch (sig) {
        case offer:
            cset = chunkID_set_init("size=10");
            if (!cset) {
                fprintf(stderr,"Unable to allocate memory for cset\n");

                return -1;
            }
            fillChunkID_set(cset, random_bmap);
            fprintf(stdout, "\"OFFER\" ");
            ret = offerChunks(dst, cset, 8, 22);
            break;
        case request:
            cset = chunkID_set_init("size=10");
            if (!cset) {
                fprintf(stderr,"Unable to allocate memory for cset\n");

                return -1;
            }
            fprintf(stdout, "\"REQUEST\" ");
            fillChunkID_set(cset,random_bmap);
            printChunkID_set(cset);
            ret = requestChunks(dst, cset, 10, 44);
            break;
        case sendbmap:
            cset = chunkID_set_init("type=bitmap,size=10");
            if (!cset) {
                fprintf(stderr,"Unable to allocate memory for cset\n");

                return -1;
            }
            fillChunkID_set(cset, random_bmap);
            fprintf(stdout, "\"SEND BMAP\" ");
            ret = sendBufferMap(dst, dst, cset, 88);
            break;
        case reqbmap:
            fprintf(stdout, "\"REQUEST BMAP\" ");
            ret = requestBufferMap(dst, NULL, 99);
            break;
        default:
            printf("Please select one operation (O)ffer, (R)equest, send (B)map, request (b)map\n");

            return -1;
    }
    fprintf(stdout, "done: %d\n", ret);
    nodeid_free(dst);
    ret = recv_from_peer(my_sock, &remote, buff, BUFFSIZE);
    if (buff[0] != MSG_TYPE_SIGNALLING) {
      fprintf(stderr, "Wrong message type!!!\n");

      return -1;
    }
    fprintf(stdout,"Received message of %d bytes\n",ret);
    ret = parseSignaling(buff+1,ret-1, &owner, &rcset, &max_deliver, &trans_id, &sig_type);
    fprintf(stdout, "Trans_id = %d; Max_del = %d\n", trans_id,max_deliver);
    switch (sig_type){
        case sig_accept:
            fprintf(stdout, "Accept of %d chunks\n", chunkID_set_size(rcset));
            break;
        case sig_deliver:
            fprintf(stdout, "Deliver of %d chunks\n", chunkID_set_size(rcset));
            break;
        case sig_send_buffermap:
            fprintf(stdout, "I received a buffer of %d chunks\n", chunkID_set_size(rcset));
            break;
    }
    printChunkID_set(rcset);
    fprintf(stdout, "Trans_id = %d; Max_del = %d\n", trans_id,max_deliver);
    if (cset) chunkID_set_free(cset);
    if (rcset) chunkID_set_free(rcset);
    if(owner)nodeid_free(owner);
    nodeid_free(remote);

    return 1;
}

int server_side(struct nodeID *my_sock)
{
    max_deliver = trans_id = ret = 0;
    ret = recv_from_peer(my_sock, &remote, buff, BUFFSIZE);

    /* TODO: Error check! */
    if (buff[0] != MSG_TYPE_SIGNALLING) {
      fprintf(stderr, "Wrong message type!!!\n");

      return -1;
    }
    fprintf(stdout,"Received message of %d bytes: trans_id %d, Max_deliver %d\n",ret,trans_id,max_deliver);
    ret = parseSignaling(buff+1,ret-1, &owner, &cset, &max_deliver, &trans_id, &sig_type);
    switch(sig_type){
        case sig_offer:
            fprintf(stdout, "1) Message OFFER: peer offers %d chunks\n", chunkID_set_size(cset));
            chunktosend = chunkID_set_get_latest(cset);
            printChunkID_set(cset);
            rcset = chunkID_set_init("size=1");
            fprintf(stdout, "2) Acceping only latest chunk #%d\n", chunktosend);
            chunkID_set_add_chunk(rcset, chunktosend);
            acceptChunks(remote, rcset, trans_id++);
            break;
        case sig_request:
            fprintf(stdout, "1) Message REQUEST: peer requests %d chunks\n", chunkID_set_size(cset));
            printChunkID_set(cset);
            chunktosend = chunkID_set_get_earliest(cset);
            rcset = chunkID_set_init("size=1");
            fprintf(stdout, "2) Deliver only earliest chunk #%d\n", chunktosend);
            chunkID_set_add_chunk(rcset, chunktosend);
            deliverChunks(remote, rcset, trans_id++);
            break;
        case sig_send_buffermap:
            fprintf(stdout, "1) Message SEND_BMAP: I received a buffer of %d chunks\n", chunkID_set_size(cset));
            printChunkID_set(cset);
            rcset = chunkID_set_init("size=15,type=bitmap");
            if (!rcset) {
                fprintf(stderr,"Unable to allocate memory for rcset\n");

                return -1;
            }
            fillChunkID_set(rcset, random_bmap);
            fprintf(stdout, "2) Message SEND_BMAP: I send my buffer of %d chunks\n", chunkID_set_size(rcset));
            printChunkID_set(rcset);
            sendBufferMap(remote, my_sock, rcset, trans_id++);
            break;
        case sig_request_buffermap:
            fprintf(stdout, "1) Message REQUEST_BMAP: Someone requeste my buffer map [%d]\n", (cset == NULL));
            rcset = chunkID_set_init("size=15,type=bitmap");
            if (!rcset) {
                fprintf(stderr,"Unable to allocate memory for rcset\n");

                return -1;
            }
            fillChunkID_set(rcset, random_bmap);
            sendBufferMap(remote, my_sock, rcset, trans_id++);
            break;
    }
    nodeid_free(remote);
    if(owner)nodeid_free(owner);
    if (cset) chunkID_set_free(cset);
    if (rcset) chunkID_set_free(rcset);

    return 1;
}

int main(int argc, char *argv[])
{
    struct nodeID *my_sock;    
    int ret;

    cmdline_parse(argc, argv);
    my_sock = init();
    ret = 0;
    if (dst_port != 0)
        ret = client_side(my_sock);
     else
        ret = server_side(my_sock);//receiver side
    nodeid_free(my_sock);

    return ret;
}

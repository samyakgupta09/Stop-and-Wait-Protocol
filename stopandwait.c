#include <cnet.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

//  DATA FRAMES CARRY A MAXIMUM-SIZED PAYLOAD, OUR MESSAGE
typedef struct {
    char        data[MAX_MESSAGE_SIZE];
} MSG;

//  THE FORMAT OF A FRAME
typedef struct {
//  THE FIRST FIELDS IN THE STRUCTURE DEFINE THE FRAME HEADER
    CnetAddr    source;	        // source address
    CnetAddr    destination;	// destination address
    int         seq;       	    // seq = -1 if the frame carries no data in msg field, else frame 					// carries a message whose sequence number is seq
    int 	    ack;		    // ack = -1 if the frame carries no ack, else frame carries an ack whose 				// sequence number is ack 
    size_t	    len;       	    // the length of the msg field only
    int         checksum;  	    // checksum of the whole frame
    CnetAddr    hops[7];        // keeps track of all the address the frame has been to
    int         lenhops;        // number of hops 

//  THE LAST FIELD IN THE FRAME IS THE PAYLOAD, OUR MESSAGE
    MSG          msg;
} FRAME;

typedef struct {
    CnetAddr    source;
    CnetAddr    destination;
    int         incoming_link;
    int         outgoing_link;
} ROUTES;

ROUTES routes[42];
int numroute = 0; 

//  THE FORMAT OF A CONNECTION
typedef struct {
    CnetAddr    destaddress;    // destination of the host with which the connection lies
    CnetTimerID lasttimer;      // last timer linked to the connection
    int         nextframe;      // seqno of the next frame to be sent
    int		    ackexpected;    // ack expected
    int 	    frameexpected;  // seqno of the next frame to be received
    FRAME*	    lastframe;      // last frame that was transmitted
    bool        is_broadcast;
} CONN;

CONN conn[7];
int numconn = 0;

void initialize_connections()
{
    for (int i = 0; i < 7; i++)
    {
        conn[i].destaddress   = -1;
        conn[i].lasttimer     = NULLTIMER;
        conn[i].nextframe     = 0;
        conn[i].ackexpected   = 0;
        conn[i].frameexpected = 0;
        conn[i].lastframe     = NULL;
        conn[i].is_broadcast  = true;
    }
}

void initialize_routes()
{
    for(int i = 0; i < 42; i++)
    {
        routes[i].incoming_link = -1;
        routes[i].outgoing_link = -1;
        routes[i].source = -1;
        routes[i].destination = -1;
    }
}


int check_conn(int destaddr)
{
    for(int i = 0; i < numconn; i++)
    {
        if(conn[i].destaddress == destaddr)
            return i;
    }
    return -1;
}

int check_route(CnetAddr source, CnetAddr destination)
{
    for(int i = 0; i < numroute; i++)
    {
        if(routes[i].source == source && routes[i].destination == destination)
        {
            return i;
        }
    }
    return -1;
}

int check_incoming(CnetAddr source, CnetAddr destination)
{
    for(int i = 0; i < numroute; i++)
    {
        if(routes[i].source == source && routes[i].destination == destination)
        {
            return routes[i].incoming_link;
        }
    }
    return -1;
}

int check_outgoing(CnetAddr source, CnetAddr destination)
{
    for(int i = 0; i < numroute; i++)
    {
        if(routes[i].source == source && routes[i].destination == destination)
        {
            return routes[i].outgoing_link;
        }
    }
    return -1;
}

bool check_hops(FRAME frame)
{
    for(int i = 0; i < frame.lenhops; i++)
    {
        if(frame.hops[i] == nodeinfo.address)
            return true;
    }
    return false;
}

//  SOME HELPFUL MACROS FOR COMMON CALCULATIONS
#define FRAME_HEADER_SIZE	(sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(frame)	(FRAME_HEADER_SIZE + frame.len)
#define increment(seq)		seq = 1-seq


//  STATE VARIABLES HOLDING INFORMATION ABOUT THE LAST MESSAGE
MSG       	lastmsg;
size_t		lastmsglength	= 0;
CnetTimerID	broadcasttimer		= NULLTIMER;

//  STATE VARIABLES HOLDING SEQUENCE NUMBERS
int 	lastdestination;

//  A FUNCTION TO TRANSMIT EITHER A DATA OR AN ACKNOWLEDGMENT FRAME
FRAME transmit_frame(CnetAddr source, CnetAddr destination, MSG *msg, size_t length, int seqno, int ack, int link, bool timer, bool broadcast, bool hop, CnetAddr hops[7], int lenhops)
{
    FRAME   frame;

    //  INITIALISE THE FRAME'S HEADER FIELDS
    frame.source        = source;
    frame.destination   = destination;
    frame.seq           = seqno;
    frame.ack           = ack;
    frame.len           = length;
    frame.checksum      = 0;
    memcpy(frame.hops, hops, sizeof(int)*7);
    frame.lenhops       = lenhops;
    if(hop)
    {
        frame.hops[lenhops] = nodeinfo.address;
        frame.lenhops++;
    }

    if(ack == -1)
    {
        if (timer)
        {
            CnetTime	timeout;
            timeout     = FRAME_SIZE(frame)*((CnetTime)8000000 / linkinfo[link].bandwidth) + linkinfo[link].propagationdelay;
            if (broadcast)
                broadcasttimer = CNET_start_timer(EV_TIMER1, 10*timeout, 0);
            else
                conn[check_conn(destination)].lasttimer = CNET_start_timer(EV_TIMER1, 10* timeout, 0);
        }
    	memcpy(&frame.msg, msg, length);
    }

//  FINALLY, WRITE THE FRAME TO THE PHYSICAL LAYER
    length		    = FRAME_SIZE(frame);
    frame.checksum	= CNET_ccitt((unsigned char *)&frame, length);
    CHECK(CNET_write_physical(link, &frame, &length));
    return frame;
}

//  THE APPLICATION LAYER HAS A NEW MESSAGE TO BE DELIVERED
EVENT_HANDLER(application_ready)
{
    CnetAddr destaddr;
    lastmsglength  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, &lastmsg, &lastmsglength));

    lastdestination = destaddr;
    CNET_disable_application(ALLNODES);

    CnetAddr hops[7] = {-1,-1,-1,-1,-1,-1,-1};
    int link = check_outgoing(nodeinfo.address, destaddr);
    int connection = check_conn(destaddr);
    if (link == -1)
    {
        printf("Link unknown, transmitting to every node\n");
        for(int i = 1; i <= nodeinfo.nlinks; i++)
        {
            printf("DATA generated and transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link: %i)\n", nodeinfo.address, destaddr, 0, -1, lastmsglength, i);
            if (i == nodeinfo.nlinks)
                transmit_frame(nodeinfo.address, destaddr, &lastmsg, lastmsglength , 0, -1, i, true, true, true, hops, 0);
            else
                transmit_frame(nodeinfo.address, destaddr, &lastmsg, lastmsglength , 0, -1, i, false, false, true, hops, 0);
        }
    }
    else
    {
        printf("DATA generated and transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link: %i)\n", nodeinfo.address, destaddr, conn[connection].nextframe, -1, lastmsglength, link);
        FRAME lastframe = transmit_frame(nodeinfo.address, destaddr, &lastmsg, lastmsglength , conn[connection].nextframe, -1, link, true, false, true, hops, 0);
        conn[connection].lastframe = &lastframe;
        conn[connection].is_broadcast = false;
    }
    
}

//  PROCESS THE ARRIVAL OF A NEW FRAME, VERIFY CHECKSUM, ACT ON ITS FRAMEKIND
EVENT_HANDLER(physical_ready)
{
    FRAME       frame;
    int         link, arriving_checksum;
    size_t	    len = sizeof(FRAME);

//  RECEIVE THE NEW FRAME
    CHECK(CNET_read_physical(&link, &frame, &len));

//  CALCULATE THE CHECKSUM OF THE ARRIVING FRAME, IGNORE IF INVALID
    arriving_checksum	= frame.checksum;
    frame.checksum  	= 0;
    if(CNET_ccitt((unsigned char *)&frame, len) != arriving_checksum) 
    {
        printf("BAD frame received: checksums (stored= %i, computed= %i)\n", arriving_checksum, CNET_ccitt((unsigned char *)&frame, len));
        return;           // bad checksum, just ignore frame
    }
    
    if(nodeinfo.address == frame.destination)
    {
        if(frame.seq == -1)
        {
            int connection = check_conn(frame.source);
            if(connection == -1)
            {
                if(frame.ack == conn[numconn].ackexpected)
                {
                    CNET_stop_timer(broadcasttimer);
                    printf("ACK received [delivered]:(src = %i, dest= %i, seq= -1, ack=%i, msgLen= 0, from link= %i)\n", frame.source, frame.destination, frame.ack, link);
                    printf("connection setup\n");
                    conn[numconn].destaddress = frame.source;
                    increment(conn[numconn].nextframe);
                    increment(conn[numconn].ackexpected);
                    numconn++;

                    routes[numroute].source = frame.source;
                    routes[numroute].destination = frame.destination;
                    routes[numroute].incoming_link = link;
                    routes[numroute].outgoing_link = link;
                    numroute++;

                    routes[numroute].source = frame.destination;
                    routes[numroute].destination = frame.source;
                    routes[numroute].incoming_link = link;
                    routes[numroute].outgoing_link = link;
                    numroute++;
                    CNET_enable_application(ALLNODES);
                }
                else
                {
                    printf("ACK not expected, ack seq no: %i\n", frame.ack);
                }
            }
            else
            {
                if(conn[connection].ackexpected == frame.ack)
                {
                    if(conn[connection].is_broadcast)
                    {
                        CNET_stop_timer(broadcasttimer);
                    }
                    else
                    {
                        CNET_stop_timer(conn[connection].lasttimer);
                    }
                    printf("ACK received [delivered]:(src = %i, dest= %i, seq= -1, ack=%i, msgLen= 0)\n", frame.source, frame.destination, frame.ack);
                    increment(conn[connection].nextframe);
                    increment(conn[connection].ackexpected);
                    CNET_enable_application(ALLNODES);
                }
                else
                {
                    printf("ACK not expected, ack seq no: %i\n", frame.ack);
                }
            }
        }
        else
        {       
            int connection = check_conn(frame.source);
            if(connection == -1)
            {
                if(conn[numconn].frameexpected == frame.seq)
                {
                    printf("DATA received [delivered]:(src= %i, dest= %i, seq= %i, ack=-1, msgLen= %li, link= %i)\n", frame.source, frame.destination, frame.seq, frame.len, link);
                    printf("ACK sent: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %i, to link: %i)\n", nodeinfo.address, frame.source, -1, frame.seq, 0, link);
                    len = frame.len;
                    CHECK(CNET_write_application(&frame.msg, &len));

                    conn[numconn].destaddress = frame.source;
                    increment(conn[numconn].frameexpected);
                    numconn++;

                    routes[numroute].source = frame.source;
                    routes[numroute].destination = frame.destination;
                    routes[numroute].incoming_link = link;
                    routes[numroute].outgoing_link = link;
                    numroute++;

                    routes[numroute].source = frame.destination;
                    routes[numroute].destination = frame.source;
                    routes[numroute].incoming_link = link;
                    routes[numroute].outgoing_link = link;
                    numroute++;
                }
                else
                {
                    printf("DATA received [delivered] and ignored, does not match seqno expected:(src= %i, dest= %i, seq= %i, ack=-1, msgLen= %li, link= %i)\n", frame.source, frame.destination, frame.seq, frame.len, link);
                    printf("ACK sent for wrong frame: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %i, to link: %i)\n", nodeinfo.address, frame.source, -1, frame.seq, 0, link);
                }
            } 
            else
            {
                if(conn[connection].frameexpected == frame.seq)
                {
                    printf("DATA received [delivered]:(src= %i, dest= %i, seq= %i, ack=-1, msgLen= %li, link= %i)\n", frame.source, frame.destination, frame.seq, frame.len, link);
                    printf("ACK sent: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %i, to link: %i)\n", nodeinfo.address, frame.source, -1, frame.seq, 0, link);
                    len = frame.len;
                    CHECK(CNET_write_application(&frame.msg, &len));
                    increment(conn[connection].frameexpected);
                }
                else
                {
                    printf("DATA received [delivered] and ignored, does not match seqno expected:(src= %i, dest= %i, seq= %i, ack=-1, msgLen= %li, link= %i)\n", frame.source, frame.destination, frame.seq, frame.len, link);
                    printf("ACK sent for wrong frame: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %i, to link: %i)\n", nodeinfo.address, frame.source, -1, frame.seq, 0, link);
                }
            }
            CnetAddr hops[7] = {-1,-1,-1,-1,-1,-1,-1};
            

            transmit_frame(frame.destination, frame.source, NULL, 0, -1, frame.seq, link, false, true, true, hops, 0);
        }
    }
    else
    {
        if(check_hops(frame))
                return;
        printf("DATA received [delivered] and relayed as destination does not match current node:(src= %i, dest= %i, seq= %i, ack=-1, msgLen= %li, from link= %i, curnode= %i)\n", frame.source, frame.destination, frame.seq, frame.len, link, nodeinfo.address);
        int route = check_route(frame.source, frame.destination);
        if(route == -1)
        {
            routes[numroute].source = frame.source;
            routes[numroute].destination = frame.destination;
            routes[numroute].incoming_link = link;
            route = numroute;
            numconn++;
        }
        int outgoing = check_outgoing(frame.source, frame.destination);
        if(outgoing == -1)
        {
            outgoing = check_incoming(frame.destination, frame.source);
            if(outgoing == -1)
            {
                printf("Outgoing link unknown, transmitting to all links\n");
                for(int i = 1; i <= nodeinfo.nlinks; i++)
                    {
                        printf("DATA transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link= %i, curnode= %i)\n", frame.source, frame.destination, frame.seq, frame.ack, frame.len, i, nodeinfo.address);
                        transmit_frame(frame.source, frame.destination, &frame.msg, frame.len, frame.seq, frame.ack, i, false, false, true, frame.hops, frame.lenhops);
                    }
            }
            else
            {
                routes[route].outgoing_link = outgoing;
                printf("Outgoing link known\n");
                printf("DATA transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link= %i, curnode= %i)\n", frame.source, frame.destination, frame.seq, frame.ack, frame.len, outgoing, nodeinfo.address);
                transmit_frame(frame.source, frame.destination, &frame.msg, frame.len, frame.seq, frame.ack, outgoing, false, false, true, frame.hops, frame.lenhops);
            }
        }
        else
        {
            printf("Outgoing link known\n");
            printf("DATA transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link= %i, curnode= %i)\n", frame.source, frame.destination, frame.seq, frame.ack, frame.len, outgoing, nodeinfo.address);
            transmit_frame(frame.source, frame.destination, &frame.msg, frame.len, frame.seq, frame.ack, outgoing, false, false, true, frame.hops, frame.lenhops);
        }
    }
}

EVENT_HANDLER(router_physical_ready)
{
    FRAME       frame;
    int         link, arriving_checksum;
    size_t	    len = sizeof(FRAME);

//  RECEIVE THE NEW FRAME
    CHECK(CNET_read_physical(&link, &frame, &len));

//  CALCULATE THE CHECKSUM OF THE ARRIVING FRAME, IGNORE IF INVALID
    arriving_checksum	= frame.checksum;
    frame.checksum  	= 0;
    if(CNET_ccitt((unsigned char *)&frame, len) != arriving_checksum) {
        printf("BAD frame received: checksums (stored= %i, computed= %i)\n", arriving_checksum, CNET_ccitt((unsigned char *)&frame, len));
        return;           // bad checksum, just ignore frame
    }

    printf("DATA received [delivered] and relayed:(src= %i, dest= %i, seq= %i, ack=-1, msgLen= %li, from link= %i, curnode= %i)\n", frame.source, frame.destination, frame.seq, frame.len, link, nodeinfo.address);
    int route = check_route(frame.source, frame.destination);
    if(route == -1)
    {
        routes[numroute].source = frame.source;
        routes[numroute].destination = frame.destination;
        routes[numroute].incoming_link = link;
        route = numroute;
        numroute++;
    }
    int outgoing = check_outgoing(frame.source, frame.destination);
    if(outgoing == -1)
    {
        outgoing = check_incoming(frame.destination, frame.source);
        if(outgoing == -1)
        {
            printf("Outgoing link unknown, transmitting to all links\n");
            for(int i = 1; i <= nodeinfo.nlinks; i++)
            {
                printf("DATA transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link= %i)\n", frame.source, frame.destination, frame.seq, frame.ack, frame.len, i);
                transmit_frame(frame.source, frame.destination, &frame.msg, frame.len, frame.seq, frame.ack, i, false, false, false, frame.hops, frame.lenhops);
            }
        }
        else
        {
            routes[route].outgoing_link = outgoing;
            printf("Outgoing link known\n");
            printf("DATA transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link= %i)\n", frame.source, frame.destination, frame.seq, frame.ack, frame.len, outgoing);
            transmit_frame(frame.source, frame.destination, &frame.msg, frame.len, frame.seq, frame.ack, outgoing, false, false, false, frame.hops, frame.lenhops);
        }
    }
    else
    {
        printf("Outgoing link known\n");
        printf("DATA transmitted: (src= %i, dest= %i, seq= %i, ack= %i, msgLen= %li, to link= %i)\n", frame.source, frame.destination, frame.seq, frame.ack, frame.len, outgoing);
        transmit_frame(frame.source, frame.destination, &frame.msg, frame.len, frame.seq, frame.ack, outgoing, false, false, false, frame.hops, frame.lenhops);
    }
}

//  WHEN A TIMEOUT OCCURS, WE RE-TRANSMIT THE MOST RECENT DATA (MESSAGE)
EVENT_HANDLER(timeouts)
{
    int link = check_outgoing(nodeinfo.address, lastdestination);
    CnetAddr hops[7] = {-1,-1,-1,-1,-1,-1,-1};
    if (link == -1)
    {
        printf("timeout for: seq=0, msglen: %li\n", lastmsglength);
        printf("retransmitting to all links\n");
        for(int i = 1; i<= nodeinfo.nlinks; i++)
        {
            if(i == nodeinfo.nlinks)
                transmit_frame(nodeinfo.address, lastdestination, &lastmsg, lastmsglength, 0, -1, i, true, true, true, hops, 0);
            else
                transmit_frame(nodeinfo.address, lastdestination, &lastmsg, lastmsglength, 0, -1, i, false, false, true, hops, 0);
        }
    }
    else
    {
        printf("timeout for: seq=%i, msglen: %li\n", conn[check_conn(lastdestination)].ackexpected, lastmsglength);
        printf("retransmitting to known link\n");
        transmit_frame(nodeinfo.address, lastdestination, &lastmsg, lastmsglength, conn[check_conn(lastdestination)].ackexpected, -1, link, true, false, true, hops, 0);
    }
    
}

//  THIS FUNCTION IS CALLED ONCE, AT THE BEGINNING OF THE WHOLE SIMULATION
EVENT_HANDLER(reboot_node)
{
    
    if(nodeinfo.nodetype == NT_HOST)
    {
        initialize_connections();
    }
    initialize_routes();

//  INDICATE THE EVENTS OF INTEREST FOR THIS PROTOCOL
    if(nodeinfo.nodetype == NT_HOST)
    {
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));
        CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
        CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    }
    else if(nodeinfo.nodetype == NT_ROUTER)
    {
        CHECK(CNET_set_handler( EV_PHYSICALREADY,    router_physical_ready, 0));
    }

//if(nodeinfo.nodenumber == 0)
	CNET_enable_application(ALLNODES);
}

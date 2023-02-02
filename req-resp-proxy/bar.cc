#include <iostream>
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sstream>

#include <foo.pb.h>

using std::atoi;

void client_routine(const std::string server)
{
    void *context = zmq_ctx_new();
    void *requester = zmq_socket(context, ZMQ_REQ);

    std::cout << "Connecting to server " << server << std::endl;
    int rc = zmq_connect(requester, server.c_str());
    assert(rc == 0);
    std::cout << "Connected to server" << std::endl;

    for(int i = 0; i < 10; i++)
    {
        // Initialize the message proto
        Message m;
        m.set_client_name("MyClient");
        m.set_payload("MyPayload");

        // Serialize the proto to string
        std::string serialized_proto;
        if(not m.SerializeToString(&serialized_proto))
        {
            std::cerr << "Error serializing proto" << std::endl;
            exit(1);
        }

        // Initialize our outbound ZMQ message and fill it with the serialized proto
        zmq_msg_t msg_out;
        if(zmq_msg_init_size (&msg_out, serialized_proto.size()))
        {
            std::cerr << "Error initializing outbound message" << std::endl;
            exit(1);
        }
        memcpy ((void *) zmq_msg_data(&msg_out), serialized_proto.c_str(), serialized_proto.size());

        std::cout << "Sending: " << m.DebugString() << " to server" << std::endl;
        // zmq_send (requester, buffer_out.c_str(), buffer_out.size(), 0);
        if(zmq_msg_send(&msg_out, requester, 0) == -1)
        {
            std::cerr << "Error sending zmq msg" << std::endl;
        }

        // Receive the potentially multi-part response, parse, print
        int more;
        size_t more_size = sizeof(more);
        zmq_msg_t msg_in;

        do
        {
            rc = zmq_msg_init(&msg_in);
            assert(rc == 0);

            int len = zmq_msg_recv(&msg_in, requester, 0);
            assert(len != -1);

            rc = zmq_getsockopt (requester, ZMQ_RCVMORE, &more, &more_size);
            assert(rc == 0);

            printf("%.*s", len, (char*)zmq_msg_data(&msg_in));

            zmq_msg_close(&msg_in);
        }
        while(more);
        printf("\n");
    }

    // Close our messages out
    // zmq_msg_close(&msg_out);
    // zmq_msg_close(&msg_in);

    // Shutdown
    zmq_close (requester);
    zmq_ctx_destroy (context);
}

void server_routine(const std::string& server)
{
    //  Socket to talk to clients
    void *context = zmq_ctx_new ();
    void *responder = zmq_socket (context, ZMQ_REP);

    std::cout << "Attempting to connect to " << server << std::endl;
    int rc = zmq_connect(responder, server.c_str());
    assert (rc == 0);
    std::cout << "Connect Success" << std::endl;

    Message m;
    while (1) {
        zmq_msg_t msg_in;
        rc = zmq_msg_init(&msg_in);
        int n = zmq_msg_recv(&msg_in, responder, 0);
        m.ParseFromArray(zmq_msg_data(&msg_in), n);
        std::cout << "Received: " << m.payload() << std::endl;

        sleep (1);          //  Do some 'work'

        /*
         * - Closing messages for sending is not necessary
         * - You must init the size each time you send a message
        */
        zmq_msg_t msg_out;

        rc = zmq_msg_init_size(&msg_out, server.size());
        assert(rc == 0);
        memcpy(zmq_msg_data(&msg_out), server.c_str(), server.size());
        rc = zmq_msg_send(&msg_out, responder, ZMQ_SNDMORE);
        assert(rc != -1);

        rc = zmq_msg_init_size(&msg_out, 1);
        assert(rc == 0);
        ((char*)zmq_msg_data(&msg_out))[0] = 'A';
        rc = zmq_msg_send(&msg_out, responder, ZMQ_SNDMORE);
        assert(rc != -1);

        rc = zmq_msg_init_size(&msg_out, 1);
        assert(rc == 0);
        ((char*)zmq_msg_data(&msg_out))[0] = 'C';
        rc = zmq_msg_send(&msg_out, responder, ZMQ_SNDMORE);
        assert(rc != -1);

        rc = zmq_msg_init_size(&msg_out, 1);
        assert(rc == 0);
        ((char*)zmq_msg_data(&msg_out))[0] = 'K';
        rc = zmq_msg_send(&msg_out, responder, 0);
        assert(rc != -1);
    }
}

void proxy_routine(const std::string& frontend_addr, const std::string& backend_addr)
{
    int rc;
    //  Prepare our context and sockets
    void *context = zmq_ctx_new ();
    void *frontend = zmq_socket (context, ZMQ_ROUTER);
    void *backend  = zmq_socket (context, ZMQ_DEALER);

    std::cout << "Frontend: " << frontend_addr << std::endl;
    std::cout << "Backend: " << backend_addr << std::endl;

    rc = zmq_bind (frontend, frontend_addr.c_str());
    if(rc != 0)
    {
        std::cerr << rc << std::endl;
        perror("Uhoh");
        exit(1);
    }
    rc = zmq_bind (backend, backend_addr.c_str());
    
    if(rc != 0)
    {
        std::cerr << rc << std::endl;
        perror("Uhoh");
        exit(1);
    }

    // zmq_proxy (frontend, backend, NULL);

    //  Initialize poll set
    zmq_pollitem_t items [] = {
        { frontend, 0, ZMQ_POLLIN, 0 },
        { backend,  0, ZMQ_POLLIN, 0 }
    };
    //  Switch messages between sockets
    while (1) {
        zmq_msg_t message;
        std::cout << "start poll" << std::endl;
        zmq_poll (items, 2, -1);
        std::cout << "Stop poll" << std::endl;
        if (items [0].revents & ZMQ_POLLIN) {
            std::cout << "Fronted got item" << std::endl;
            while (1) {
                //  Process all parts of the message
                zmq_msg_init (&message);
                zmq_msg_recv (&message, frontend, 0);
                int more = zmq_msg_more (&message);
                zmq_msg_send (&message, backend, more? ZMQ_SNDMORE: 0);
                zmq_msg_close (&message);
                if (!more)
                    break;      //  Last message part
            }
        }
        if (items [1].revents & ZMQ_POLLIN) {
            std::cout << "Backend got item" << std::endl;
            while (1) {
                //  Process all parts of the message
                zmq_msg_init (&message);
                zmq_msg_recv (&message, backend, 0);
                int more = zmq_msg_more (&message);
                zmq_msg_send (&message, frontend, more? ZMQ_SNDMORE: 0);
                zmq_msg_close (&message);
                if (!more)
                    break;      //  Last message part
            }
        }
    }
    //  We never get here, but clean up anyhow
    zmq_close (frontend);
    zmq_close (backend);
    zmq_ctx_destroy (context);
}

int main(int argc, char** argv) {
    if(std::string(argv[1]) == "client")
    {
        std::string server(argv[2]);
        std::cout << "Running as client, connecting to the folloring server: " << server << std::endl;
        
        client_routine(server);
    }
    else if(std::string(argv[1]) == "server")
    {
        std::string server(argv[2]);
        std::cout << "Running as server, connecting to the following server: " << server << std::endl;
        
        server_routine(server);
    }
    else if(std::string(argv[1]) == "proxy")
    {
        std::string frontend_addr(argv[2]);
        std::string backend_addr(argv[3]);

        std::cout << "Running as proxy" << std::endl;
        
        proxy_routine(frontend_addr, backend_addr);
    }
    else
    {
        std::cout << "Usage: client|server" << std::endl;
        exit(1);
    }
        


    return 0;
}
#include "VisionReceiver.hpp"

#include <unistd.h>
#include <QMutexLocker>
#include <QUdpSocket>
#include <Utils.hpp>
#include <multicast.hpp>
#include <stdexcept>

using namespace std;

VisionReceiver::VisionReceiver(bool sim, int port)
    : simulation(sim), _running(false), port(port) {
    this->setObjectName("VisionReceiver");
}

void VisionReceiver::stop() {
    if (isRunning()) {
        // Handle Case where start has been called, but _running has not been
        // set to true yet.
        while (!_running) {
        }
        _running = false;
        wait();
    }
}

void VisionReceiver::getPackets(std::vector<VisionPacket*>& packets) {
    _mutex.lock();
    packets = _packets;
    _packets.clear();
    _mutex.unlock();
}

void VisionReceiver::run() {
    _running = true;
    QUdpSocket socket;

    // Create vision socket
    if (simulation) {
        if (!socket.bind(SimVisionPort, QUdpSocket::ShareAddress)) {
            throw runtime_error("Can't bind to shared vision port");
        }

        multicast_add(&socket, SharedVisionAddress);
    } else {
        // Receive multicast packets from shared vision.
        if (!socket.bind(port, QUdpSocket::ShareAddress)) {
            throw runtime_error("Can't bind to shared vision port");
        }

        multicast_add(&socket, SharedVisionAddress);
    }

    // There should be at most four packets: one for each camera on each of two
    // frames, assuming some clock skew between this
    // computer and the vision computer.
    _packets.reserve(4);
    while (_running) {
        char buf[65536];

        // Wait for a UDP packet
        if (!socket.waitForReadyRead(500)) {
            // Time out once in a while so the thread has a chance to exit
            continue;
        }

        QHostAddress host;
        quint16 portNumber = 0;
        qint64 size = socket.readDatagram(buf, sizeof(buf), &host, &portNumber);
        if (size < 1) {
            fprintf(stderr, "VisionReceiver: %s\n",
                    (const char*)socket.errorString().toLatin1());
            // See Processor for why we can't use QThread::msleep()
            ::usleep(100 * 1000);
            continue;
        }

        // FIXME - Verify that it is from the right host, in case there are
        // multiple visions on the network

        // Parse the protobuf message
        VisionPacket* packet = new VisionPacket;
        packet->receivedTime = RJ::now();
        if (!packet->wrapper.ParseFromArray(buf, size)) {
            fprintf(stderr,
                    "VisionReceiver: got bad packet of %d bytes from %s:%d\n",
                    (int)size, (const char*)host.toString().toLatin1(),
                    portNumber);
            continue;
        }

        // Add to the vector of packets
        _mutex.lock();
        _packets.push_back(packet);
        _mutex.unlock();
    }
}

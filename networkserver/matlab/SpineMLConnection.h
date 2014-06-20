/* -*-c++-*- */

/*
 * This is the connection class for connections to SpineML (as
 * generated by SpineCreator).
 *
 * This code is used by spinemlnetStart.cpp, a matlab mex function,
 * and friends. spinemlnetStart.cpp creates a main thread which
 * listens for incoming TCP/IP connections. When a new connection is
 * received, the main thread creates a SpineMLConnection object, which
 * has its own thread.
 *
 * This class contains the data relating to the connection; the
 * numbers being transferred to and from the SpineML experiment. It
 * also holds a reference to its thread and manages the handshake and
 * associated information (data direction, type, etc).
 *
 * The connection state starts out as !established and !failed. Once
 * the handshake with the SpineML client is completed, established is
 * set, and clientDataDirection etc should all be set. If comms with
 * the client fail, failed is set true, which will allow the main
 * thread to clean the connection up. Once the connections are all
 * finished, the finished flag is set.
 *
 * Note that this code is all in a single header; implementation as
 * well as class declaration. This keeps the compilation of the mex
 * functions very simple (i.e. no linking), with the small
 * disadvantage that this code gets re-compiled for every mex function
 * which #includes it.
 */

#ifndef _SPINEMLCONNECTION_H_
#define _SPINEMLCONNECTION_H_

#include <iostream>
#include <deque>
#include <vector>

extern "C" {
#include <unistd.h>
#include <errno.h>
}

#include "SpineMLDebug.h"

using namespace std;

// SpineML tcp/ip comms flags.
#define RESP_DATA_NUMS     31 // a non-printable character
#define RESP_DATA_SPIKES   32 // ' ' (space)
#define RESP_DATA_IMPULSES 33 // '!'
#define RESP_HELLO         41 // ')'
#define RESP_RECVD         42 // '*'
#define RESP_ABORT         43 // '+'
#define RESP_FINISHED      44 // ','
#define AM_SOURCE          45 // '-'
#define AM_TARGET          46 // '.'
#define NOT_SET            99 // 'c'

// SpineML tcp/ip comms data types
enum dataTypes {
    ANALOG,
    EVENT,
    IMPULSE
};

// Handshake stages:
#define CS_HS_GETTINGTARGET     0
#define CS_HS_GETTINGDATATYPE   1
#define CS_HS_GETTINGDATASIZE   2
#define CS_HS_GETTINGNAME       3
#define CS_HS_DONE              4

// How many times to fail to read a byte before calling the session a
// failure:
#define NO_DATA_MAX_COUNT     100

// See spinemlnetStart.cpp, which instatates dataCache at global scope.
#ifdef DATACACHE_MAP_DEFINED
// We have a "real" data cache object, externally defined, probably in
// the mex cpp file.
extern map<string, deque<double>*>* dataCache;
extern pthread_mutex_t dataCacheMutex;
#else
// We need a dummy dataCache object.
map<string, deque<double>*>* dataCache = (map<string, deque<double>*>*)0;
pthread_mutex_t dataCacheMutex;
#endif

/*!
 * A connection class. The SpineML client code connects to this server
 * with a separate connection for each stream of data. For example,
 * population A makes one connection to obtain its input, population B
 * makes a second connection for input. population C makes an output
 * connection. This class holds the file descriptor of the connection,
 * plus information (obtained during the connection handshake) about
 * the data direction, data type and data size.
 *
 * Each one of these connections is expected to run on a separate
 * thread, which means you can use blocking i/o for reading and
 * writing to the network.
 */
class SpineMLConnection
{
public:

    /*!
     * The constructor initialises some variables and initialises the
     * data mutex.
     */
    SpineMLConnection()
        : connectingSocket (0)
        , established (false)
        , failed (false)
        , finished (false)
        , unacknowledgedDataSent (false)
        , noData (0)
        , clientConnectionName ("")
        , clientDataDirection (NOT_SET)
        , clientDataType (NOT_SET)
        , clientDataSize (1)
        , data ((deque<double>*)0)
        , doublebuf ((double*)0)
        {
            pthread_mutex_init (&this->dataMutex, NULL);
        };

    /*!
     * The destructor closes the connecting socket (if necessary) then
     * destroys the data mutex.
     */
    ~SpineMLConnection()
        {
            if (this->connectingSocket > 0) {
                this->closeSocket();
            }
            pthread_mutex_destroy(&this->dataMutex);
            if (this->data != (deque<double>*)0) {
                delete this->data;
            }
            if (this->doublebuf != (double*)0) {
                delete this->doublebuf;
            }
        };

    /*!
     * Simple accessors
     */
    //@{
    int getConnectingSocket (void);
    void setConnectingSocket (int i);
    char getClientDataDirection (void);
    char getClientDataType (void);
    string getClientConnectionName (void);
    unsigned int getClientDataSize (void);
    bool getEstablished (void);
    bool getFailed (void);
    bool getFinished (void);
    //@}

    /*!
     * Go through the handshake process, as defined in protocol.txt.
     *
     * There are 4 stages in the handshake process: "initial
     * handshake", "set datatype", "set datasize" and "set connection
     * name".
     *
     * Returns 0 on success, -1 on failure.
     */
    int doHandshake (void);

    /*!
     * If the client has data for us, then read it.
     *
     * Returns 0 on success, -1 on failure and 1 if the connection
     * completed.
     */
    int doReadFromClient (void);

    /*!
     * If we have data to write, then write it to the client.
     *
     * Returns 0 on success, -1 on failure and 1 if the connection
     * completed.
     */
    int doWriteToClient (void);

    /*!
     * Perform input/output with the client. This will call either
     * doWriteToClient or doReadFromClient.
     *
     * Returns 0 on success, -1 on failure and 1 if the connection
     * completed.
     */
    int doInputOutput (void);

    /*!
     * Close the connecting socket, set the connectingSocket value to
     * 0 and set established to false.
     */
    void closeSocket (void);

    /*!
     * Obtain a lock on the data mutex.
     */
    void lockDataMutex (void);

    /*!
     * Release lock on the data mutex.
     */
    void unlockDataMutex (void);

    /*!
     * Add the double precision number d to the data deque, using
     * push_back().
     */
    void addNum (double& d);

    /*!
     * Add dataSize elements from the double array d to the data
     * deque. Calls push_back dataSize times.
     */
    void addData (double* d, size_t dataSize);

    /*!
     * Return the number of data elements in data - the number of
     * doubles in this->data. Don't confuse with clientDataSize, which
     * is the number of doubles to transfer per timestep.
     */
    size_t getDataSize (void);

    /*!
     * Pop a value from the front of the data deque and return
     * it. This returns this->data->at(0) and calls
     * this->data->pop_front();
     *
     * May throw std::out_of_range.
     */
    double popFront (void);

public:

    /*!
     * The thread on which this connection will execute.
     */
    pthread_t thread;

private:

    /*!
     * The file descriptor of the TCP/IP socket on which this
     * connection is running.
     */
    int connectingSocket;

    /*!
     * Set to true once the connection is fully established and the
     * handshake is complete.
     */
    bool established;

    /*!
     * Set to true if the connection fails - this will be due to a
     * failed read or write call.
     */
    bool failed;

    /*!
     * Set to true if the connection finishes - the client will disconnect.
     */
    bool finished;

    /*!
     * Every time data is sent to the client, set this to true. When a
     * RESP_RECVD response is received from the client, set this back
     * to false.
     */
    bool unacknowledgedDataSent;

    /*!
     * Used as a counter for when no data is received via a
     * connection.
     */
    unsigned int noData;

    /*!
     * The name of the connection, as defined by the client.
     */
    string clientConnectionName;

    /*!
     * The data direction, as set by the client. Client sends a flag
     * which is either AM_SOURCE (I am a source) or AM_TARGET (I am a
     * target).
     */
    char clientDataDirection; // AM_SOURCE or AM_TARGET

    /*!
     * There are 3 possible data types; nums(analog), spikes(events)
     * or impulses. Only nums implemented.
     */
    char clientDataType;

    /*!
     * The data size. This is the number of data per timestep. For
     * nums, that means it's the number of doubles per timestep.
     */
    unsigned int clientDataSize;

    /*!
     * A mutex for access to the data deque.
     */
    pthread_mutex_t dataMutex;

    /*!
     * The data which is accessed on the matlab side. This is a double
     * ended queue, and we use it as a first-in first-out
     * container. Data coming into the class object is pushed to the
     * back of the deque; data being retrieved from the object is
     * popped from the front.
     *
     * Note that this is a pointer to the data. The data may be
     * allocated by this class the first time it is required, or it
     * may be pre-allocated if data was passed in by the matlab user
     * prior to the establishment of the connection - i.e. prior to
     * the instantiation of an object of this class which matches the
     * connection name.
     */
    std::deque<double>* data;

    /*!
     * A small buffer for use with data comms.
     */
    char smallbuf[16];

    /*!
     * A buffer used for reading data from the TCP/IP wire. Data is
     * read into this buffer, then transferred into the deque
     * data. This buffer is allocated during the connection handshake,
     * after the data size has been successfully received from the
     * client.
     */
    double* doublebuf;
};

/*!
 * Accessor implementations
 */
//@{
int
SpineMLConnection::getConnectingSocket (void)
{
    return this->connectingSocket;
}
void
SpineMLConnection::setConnectingSocket (int i)
{
    this->connectingSocket = i;
}
char
SpineMLConnection::getClientDataDirection (void)
{
    return this->clientDataDirection;
}
char
SpineMLConnection::getClientDataType (void)
{
    return this->clientDataType;
}
string
SpineMLConnection::getClientConnectionName (void)
{
    return this->clientConnectionName;
}
unsigned int
SpineMLConnection::getClientDataSize (void)
{
    return this->clientDataSize;
}
bool
SpineMLConnection::getEstablished (void)
{
    return this->established;
}
bool
SpineMLConnection::getFailed (void)
{
    return this->failed;
}
bool
SpineMLConnection::getFinished (void)
{
    return this->finished;
}
//@}

int
SpineMLConnection::doHandshake (void)
{
    ssize_t b = 0;
    // There are four stages in the handshake process, starting with this one:
    int handshakeStage = CS_HS_GETTINGTARGET;

    this->noData = 0;
    while (handshakeStage != CS_HS_DONE && this->noData < NO_DATA_MAX_COUNT) {

        // What stage are we at in the handshake?
        if (handshakeStage == CS_HS_GETTINGTARGET) {
            b = read (this->connectingSocket, (void*)this->smallbuf, 1);
            if (b == 1) {
                // Got byte.
                if (this->smallbuf[0] == AM_SOURCE || this->smallbuf[0] == AM_TARGET) {
                    this->clientDataDirection = this->smallbuf[0];
                    // Write response.
                    this->smallbuf[0] = RESP_HELLO;
                    if (write (this->connectingSocket, this->smallbuf, 1) != 1) {
                        INFO ("SpineMLConnection::doHandshake: "
                              << "Failed to write RESP_HELLO to client.");
                        this->failed = true;
                        return -1;
                    }
                    // Success, increment handshake stage.
                    handshakeStage++;
                    this->noData = 0; // reset the "no data" counter now.
                } else {
                    // Wrong data direction.
                    this->clientDataDirection = NOT_SET;
                    INFO ("SpineMLConnection::doHandshake: "
                          << "Wrong data direction in first handshake byte from client.");
                    this->failed = true;
                    return -1;
                }
            } else {
                // No byte read, increment the no_data counter.
                ++this->noData;
            }

        } else if (handshakeStage == CS_HS_GETTINGDATATYPE) {
            b = read (this->connectingSocket, (void*)this->smallbuf, 1);
            if (b == 1) {
                // Got byte.
                if (this->smallbuf[0] == RESP_DATA_NUMS) {
                    this->clientDataType = this->smallbuf[0];
                    this->smallbuf[0] = RESP_RECVD;
                    if (write (this->connectingSocket, this->smallbuf, 1) != 1) {
                        INFO ("SpineMLConnection::doHandshake: "
                              "Failed to write RESP_RECVD to client.");
                        this->failed = true;
                        return -1;
                    }
                    handshakeStage++;
                    this->noData = 0;

                } else if (this->smallbuf[0] == RESP_DATA_SPIKES
                           || this->smallbuf[0] == RESP_DATA_IMPULSES) {
                    // These are not yet implemented.
                    INFO ("SpineMLConnection::doHandshake: Spikes/Impulses not yet implemented.");
                    this->failed = true;
                    return -1;

                } else {
                    // Wrong/unexpected character.
                    INFO ("SpineMLConnection::doHandshake: Data type flag "
                          << (int)this->smallbuf[0] << " is unexpected here.");
                    this->failed = true;
                    return -1;
                }
            } else {
                if (this->noData < 10) {
                    INFO ("SpineMLConnection::doHandshake: Got " << b << " bytes, not 1");
                }
                ++this->noData;
            }

        } else if (handshakeStage == CS_HS_GETTINGDATASIZE) {
            b = read (this->connectingSocket, (void*)this->smallbuf, 4);
            if (b == 4) {
                // Got 4 bytes. This is the data size - the number of
                // doubles to transmit during each timestep. E.g.: If
                // a population has 10 neurons, then this will be
                // 10. Interpret as an unsigned int, with the first
                // byte in the buffer as the least significant byte:
                this->clientDataSize =
                    (unsigned char)this->smallbuf[0]
                    | (unsigned char)this->smallbuf[1] << 8
                    | (unsigned char)this->smallbuf[2] << 16
                    | (unsigned char)this->smallbuf[3] << 24;

                INFO ("SpineMLConnection::doHandshake: client data size: "
                     << this->clientDataSize << " doubles/timestep");

                // Can now allocate doublebuf.
                this->doublebuf = new double[clientDataSize];

                this->smallbuf[0] = RESP_RECVD;
                if (write (this->connectingSocket, this->smallbuf, 1) != 1) {
                    INFO ("SpineMLConnection::doHandshake: Failed to write RESP_RECVD to client.");
                    this->failed = true;
                    return -1;
                }

                handshakeStage++;
                this->noData = 0;

            } else if (b > 0) {
                // Wrong number of bytes.
                INFO ("SpineMLConnection::doHandshake: Read " << b << " bytes, expected 4.");
                this->failed = true;
                return -1;

            } else {
                ++this->noData;
            }

        } else if (handshakeStage == CS_HS_GETTINGNAME) {
            b = read (this->connectingSocket, (void*)this->smallbuf, 4);
            if (b == 4) {
                // Got 4 bytes. This is the size of the name - the
                // number of chars to read from the name.
                int nameSize =
                    (unsigned char)this->smallbuf[0]
                    | (unsigned char)this->smallbuf[1] << 8
                    | (unsigned char)this->smallbuf[2] << 16
                    | (unsigned char)this->smallbuf[3] << 24;

                // sanity check
                if (nameSize > 1024) {
                    INFO ("SpineMLConnection::doHandshake: Insanely long name ("
                          << nameSize << " bytes)");
                    this->failed = true;
                    return -1;
                }

                // Now we know how much to read for the name.
                char namebuf[1+nameSize];
                b = read (this->connectingSocket, (void*)namebuf, nameSize);
                if (b != nameSize) {
                    // Wrong number of bytes.
                    INFO ("SpineMLConnection::doHandshake: Read " << b
                          << " bytes, expected " << nameSize);
                    this->failed = true;
                    return -1;
                } // else carry on...

                // We got the name; great.
                namebuf[nameSize] = '\0';
                this->clientConnectionName.assign (namebuf);
                INFO ("SpineMLConnection::doHandshake: Connection name is '"
                      << this->clientConnectionName << "'");
                this->smallbuf[0] = RESP_RECVD;
                if (write (this->connectingSocket, this->smallbuf, 1) != 1) {
                    INFO ("SpineMLConnection::doHandshake: Failed to write RESP_RECVD to client.");
                    this->failed = true;
                    return -1;
                }

                // Now we have the name, lets see if any data has been
                // supplied for this connection already and stored in
                // dataCache.
                if (dataCache != (map<string, deque<double>*>*)0) {
                    pthread_mutex_lock (&dataCacheMutex);
                    map<string, deque<double>*>::iterator entry = dataCache->find(this->clientConnectionName);
                    if (entry != dataCache->end()) {
                        INFO ("Using cached data for connection '" << this->clientConnectionName << "'");
                        // Use connectionName->at(this->clientConnectionName).second as data.
                        this->data = entry->second;
                        INFO ("data contains " << this->data->size() << " doubles.");
                        // Now remove the entry from dataCache, as the
                        // data is now in the connection:
                        dataCache->erase (entry);
                        INFO ("data contains " << this->data->size() << " doubles.");
                    } else {
                        // No pre-existing data; allocate new data
                        INFO ("No cached data for connection '" << this->clientConnectionName << "', allocate new store.");
                        this->data = new deque<double>();
                    }
                    pthread_mutex_unlock (&dataCacheMutex);
                } else {
                    // There's no dataCache object, go straight to allocating new data.
                    INFO ("Allocating new data store for this connection.");
                    this->data = new deque<double>();
                }

                handshakeStage++;
                this->noData = 0;

            } else if (b > 0) {
                // Wrong number of bytes.
                INFO ("SpineMLConnection::doHandshake: Read " << b << " bytes; expected 4.");
                this->failed = true;
                return -1;
            } else {
                ++this->noData;
            }

        } else if (handshakeStage == CS_HS_DONE) {
            INFO ("SpineMLConnection::doHandshake: Handshake finished.");

        } else {
            INFO ("SpineMLConnection::doHandshake: Error: Invalid handshake stage.");
            this->failed = true;
            return -1;
        }
    }

    if (this->noData >= NO_DATA_MAX_COUNT) {
        INFO ("SpineMLConnection::doHandshake: Error: Failed to get data from client.");
        this->failed = true;
        return -1;
    }

    // This connection is now established:
    this->established = true;

    return 0;
}

void
SpineMLConnection::lockDataMutex (void)
{
    pthread_mutex_lock (&this->dataMutex);
}

void
SpineMLConnection::unlockDataMutex (void)
{
    pthread_mutex_unlock (&this->dataMutex);
}

int
SpineMLConnection::doReadFromClient (void)
{
    // NB: Can't read directly into this->data, as it is not backed by
    // contiguous memory region.
    size_t datachunk = sizeof(double)*this->clientDataSize;
    int b = read (this->connectingSocket, this->doublebuf, datachunk);
    if (b < 0) {
        int theError = errno;
        INFO ("SpineMLConnection::doReadFromClient: Read wrong number of bytes ("
              << b << " not " << datachunk << "). errno: "
              << theError);
        return -1;
    } else if (b == datachunk) {
        // Correct amount of data was read. Transfer it into data.
        this->lockDataMutex();
        int i = 0;
        double* p = this->doublebuf;
        while (i<this->clientDataSize) {
            this->data->push_back (*p++);
            ++i;
        }
        this->noData = 0;
        this->unlockDataMutex();
    } else if (b == 0 && this->noData < NO_DATA_MAX_COUNT) {
        ++this->noData;
        return 0;
    } else if (b == 0 && this->noData == NO_DATA_MAX_COUNT) {
        // Nothing was available to read. Need to count up a-la
        // doWriteToClient and then say "ok, done".
        INFO ("SpineMLConnection:doReadFromClient: No data available, assume finished.");
        return 1;
    }

    // Now write RESP_RECVD
    this->smallbuf[0] = RESP_RECVD;
    if (write (this->connectingSocket, this->smallbuf, 1) != 1) {
        int theError = errno;
        INFO ("SpineMLConnection::doReadFromClient: Failed to write RESP_RECVD to client. errno: "
              << theError);
        if (theError == ECONNRESET) {
            // This isn't really an error - it means the client disconnected.
            return 1;
        } else {
            return -1;
        }
    }

    return 0;
}

int
SpineMLConnection::doWriteToClient (void)
{
    // Expect an acknowledgement from the client if we sent data:
    if (this->unacknowledgedDataSent == true) {
        int b = read (this->connectingSocket, (void*)this->smallbuf, 1);
        if (b == 1) {
            // Good; we got data.
            if (this->smallbuf[0] != RESP_RECVD) {
                INFO ("SpineMLConnection::doWriteToClient: Wrong response from client.");
                return -1;
            }
            // Got the acknowledgement, set this to false again:
            this->unacknowledgedDataSent = false;
            // And reset our noData variable:
            this->noData = 0;

        } else if (b == 0 && this->noData < NO_DATA_MAX_COUNT) {
            // No data available yet, so simply return 0; that means
            // we'll come back to trying to read the acknowledgement
            // later.
            DBG2 ("SpineMLConnection::doWriteToClient: No data on wire right now.");
            this->noData++;
            return 0;

        } else if (b == 0 && this->noData == NO_DATA_MAX_COUNT) {
            int theError = errno;
            INFO ("SpineMLConnection::doWriteToClient: Failed to read RESP_RECVD from client. "
                  << "Hit max number of tries. errno: " << theError);
            if (theError == 0) {
                // Ok, there's just no data.
                return 1;
            } else {
                return -1;
            }

        } else {
            int theError = errno;
            INFO ("SpineMLConnection::doWriteToClient: Failed to read 1 byte from client. errno: "
                 << theError << " bytes read: " << b);
            if (theError == ECONNRESET) {
                // This isn't really an error - it means the client disconnected.
                return 1;
            } else {
                return -1;
            }
        }
    } // else we're not waiting for a RESP_RECVD response from the server.

    // We're going to update some data in memory
    this->lockDataMutex();
    if (this->data->size() >= this->clientDataSize) {

        // We have enough data to write some to the client:
        int j = 0;
        double* p = this->doublebuf;
        while (j < this->clientDataSize) {
            *p++ = this->data->at(0);
            ++j; this->data->pop_front();
        }
        ssize_t bytesWritten = write (this->connectingSocket,
                                      this->doublebuf,
                                      this->clientDataSize*sizeof(double));

        if (bytesWritten != this->clientDataSize*sizeof(double)) {
            int theError = errno;
            INFO ("SpineMLConnection::doWriteToClient: Failed. Wrote " << bytesWritten
                  << " bytes. Tried to write " << (this->clientDataSize*sizeof(double))
                  << ". errno: " << theError);
            // Note: We'll get ECONNRESET (errno 104) when the client
            // has finished its experiment and needs no more data.
            this->unlockDataMutex();
            return -1;
        } // else carry on

        DBG2 ("SpineMLConnection::doWriteToClient: wrote " << bytesWritten << " bytes.");

        // Set that we now need an acknowledgement from the client:
        this->unacknowledgedDataSent = true;

        this->noData = 0;

    } else {
        // No more data to write
        if (this->noData >= NO_DATA_MAX_COUNT) {
            INFO ("SpineMLConnection::doWriteToClient: "
                  << "No data left to write to connection '"
                  << this->clientConnectionName << "', assume finished.");
            this->unlockDataMutex();
            return 1;
        }
        this->noData++;
    }
    this->unlockDataMutex();

    return 0;
}

int
SpineMLConnection::doInputOutput (void)
{
    // Check if this is an established connection.
    if (this->established == false) {
        DBG ("SpineMLConnection::doInputOutput: connection is not established, returning 0.");
        return 0;
    }

    // Update the buffer by reading/writing from network.
    if (this->clientDataDirection == AM_TARGET) {
        // Client is a target, I need to write data to the client, if I have anything to write.
        DBG2 ("SpineMLConnection::doInputOutput: clientDataDirection: AM_TARGET.");
        int drc = this->doWriteToClient();
        if (drc == -1) {
            INFO ("SpineMLConnection::doInputOutput: Error writing to client.");
            this->failed = true;
            this->finished = true;
            return -1;
        } else if (drc == 1) {
            // Client disconnected.
            DBG2 ("SpineMLConnection::doInputOutput: Client disconnected.");
            this->finished = true;
            return 1;
        }

    } else if (this->clientDataDirection == AM_SOURCE) {
        // Client is a source, I need to read data from the client.
        DBG2 ("SpineMLConnection::doInputOutput: clientDataDirection: AM_SOURCE.");
        int drc = this->doReadFromClient();
        if (drc == -1) {
            INFO ("SpineMLConnection::doInputOutput: Error reading from client.");
            this->failed = true;
            this->finished = true;
            return -1;
        } else if (drc == 1) {
            // Client disconnected.
            DBG2 ("SpineMLConnection::doInputOutput: Client disconnected.");
            this->finished = true;
            return 1;
        }

    } else {
        // error.
        INFO ("SpineMLConnection::doInputOutput: clientDataDirection has wrong value: "
             << (int)this->clientDataDirection);
    }

    return 0;
}

void
SpineMLConnection::closeSocket (void)
{
    if (close (this->connectingSocket)) {
        int theError = errno;
        INFO ("SpineMLConnection::closeSocket: Error closing connecting socket: " << theError);
    } else {
        this->connectingSocket = 0;
    }
    this->established = false;
}

void
SpineMLConnection::addNum (double& d)
{
    // Don't allow addition of data if not established.
    if (!this->established || this->failed) {
        return;
    }
    this->lockDataMutex();
    this->data->push_back (d);
    this->unlockDataMutex();
}

void
SpineMLConnection::addData (double* d, size_t dataSize)
{
    if (!this->established || this->failed) {
        return;
    }
    this->lockDataMutex();
    size_t i = 0;
    while (i < dataSize) {
        this->data->push_back (*d);
        ++d;
        ++i;
    }
    this->unlockDataMutex();
}

size_t
SpineMLConnection::getDataSize (void)
{
    this->lockDataMutex();
    size_t sz = this->data->size();
    this->unlockDataMutex();
    return sz;
}

double
SpineMLConnection::popFront (void)
{
    double rtn;
    this->lockDataMutex();
    rtn = this->data->at(0);
    this->data->pop_front();
    this->unlockDataMutex();
    return rtn;
}
#endif // _SPINEMLCONNECTION_H_

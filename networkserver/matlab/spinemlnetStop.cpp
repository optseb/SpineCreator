/*
 * Stop tcpip server.
 */

#include "mex.h"
#include <iostream>

using namespace std;

void
mexFunction (int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    cout << "SpineMLNet: stop-mexFunction: Called" << endl;
    unsigned long long int *threadPtr; // Or some other type?
    if (nrhs==0) {
       mexErrMsgTxt("failed");
    }

    threadPtr = (unsigned long long int*)mxGetData(prhs[0]);
    pthread_t *thread = ((pthread_t*) threadPtr[0]);
    volatile bool *stopRequested = ((volatile bool*) threadPtr[1]);
    pthread_mutex_t *bufferMutex = ((pthread_mutex_t *) threadPtr[4]);

    // request termination
    *stopRequested = true;
    // wait for thread to terminate
    cout << "SpineMLNet: stop-mexFunction: Wait for thread to join..." << endl;
    pthread_join(*thread, 0);
    cout << "SpineMLNet: stop-mexFunction: Thread has terminated, destroy mutexes then return." << endl;

    // destroy mutexes
    pthread_mutex_destroy(bufferMutex);
    cout << "SpineMLNet: stop-mexFunction: Returning" << endl;
}

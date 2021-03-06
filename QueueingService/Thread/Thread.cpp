#include "stdafx.h"
#include "Thread.h"
#include "Util.h"
#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27016"
#define SERVER_PORT "27017"
#define SERVER_PORT_INT 27017



int pushInBuffer(char * recvbuf, MySocket * acceptedSocket, Queue * queue, SOCKET * socket, SocketArray *socketArray) {
	int nameSize = DataNameSize(recvbuf);
	char* name = (char *)malloc(sizeof(char)*nameSize + 1);
	Buffer * buffer;
	CRITICAL_SECTION cs;
	InitializeCriticalSection(&cs);
	parseMessage(name, nameSize, recvbuf, &cs);


	/* Ako je u pitanju samo konekcija, ovde zavrsi nit. */
	char c = getCharacter(recvbuf, &cs);
	if (c == 'c') {
		DeleteCriticalSection(&cs);
		acceptedSocket->bufferName = name;
		return -1;
	}
	// return;
	DeleteCriticalSection(&cs);
	//pronadjemo bufer koji nam treba, 
	// TODO: treba videti sta ako ne pronadje odgovarajuci bafer!
	if(socket != NULL)
		buffer = findBuffer(queue, strcat(name,"1"));
	else{
		buffer = findBuffer(queue, name);
		acceptedSocket->bufferName = name;
	}
	//free(name); //////////////////////////////////////////FREE
				//TODO: Gde se free ovo ime??
	push(buffer, recvbuf); //punimo bafer

	return 0;
}

DWORD WINAPI PushInService(LPVOID lpParam)
{
	PushParams pushParams = *(PushParams*)lpParam;
	MySocket *acceptedSocket = pushParams.socket;
	SocketArray *socketArray = pushParams.socketArray;
	int initializator = pushParams.initializator;
	int iResult = 0;

	char recvbuf[DEFAULT_BUFLEN];
	memset(recvbuf, 0, DEFAULT_BUFLEN);
	do
	{
		// Receive data until the client shuts down the connection
		//iResult = recv(acceptedSocket, recvbuf, DEFAULT_BUFLEN, 0);
		
		iResult = RECEIVE(&acceptedSocket->socket, recvbuf);
		//recvbuf je data!!!

		if (iResult > 0)
		{
			//uzmemo ime iz poruke
			iResult = pushInBuffer(recvbuf, acceptedSocket, pushParams.queue, NULL, socketArray);
			if (iResult == -1) {

				return 0;


			}

		}
		else if (iResult == 0)
		{
			/*
				Kada iResult padne na nulu, receive je zavrsen, potrebno je poslati poruku na drugi servis!
				*/
			// connection was closed gracefully
			printf("Connection with client closed.\n");
			closesocket(acceptedSocket->socket);
			acceptedSocket->socket = INVALID_SOCKET;
			//free(acceptedSocket->bufferName);
			acceptedSocket->bufferName = NULL;
		}
		else
		{
			// there was an error during recv
			printf("recv failed with error: %d\n", WSAGetLastError());
			acceptedSocket->socket = INVALID_SOCKET;
			free(acceptedSocket->bufferName);
			acceptedSocket->bufferName = NULL;
		}
	} while (iResult > 0);

	return 0;
}

int sendOnSocket(Buffer *buffer, SOCKET *socket, char *data, MySocket *mySocket) {

	pop(buffer, data);

	// Send an prepared message with null terminator included

	if (mySocket != NULL) {
		int iResult = sendMessage(&mySocket->socket, data);

		if (iResult == SOCKET_ERROR)
		{
			printf("send failed with error: %d\n", WSAGetLastError());
			closesocket(mySocket->socket);
			//WSACleanup();
			return 1;
		}
		/*
		Zatvori konekciju sa klijentom.
		*/
		closesocket(mySocket->socket);
		mySocket->socket = INVALID_SOCKET;
		mySocket->bufferName = NULL;
		printf("Bytes Sent: %ld\n", iResult);
	}
	else {
		int iResult = sendMessage(socket, data);

		if (iResult == SOCKET_ERROR)
		{
			printf("send failed with error: %d\n", WSAGetLastError());
			closesocket(mySocket->socket);
			//WSACleanup();
			return 1;
		}
		//printf("Bytes Sent: %ld\n", iResult);
	}


	return 0;

}

DWORD WINAPI PopFromService(LPVOID lpParam)
{
	PPSParams* ppsParams = (PPSParams*)lpParam;
	Queue *queue = ppsParams->queue;
	SocketArray *socketArray = ppsParams->socketArray;
	int type = ppsParams->type;
	SOCKET *serviceSocket = ppsParams->serviceSocket;

	/*
	
		Prodji kroz sve redove u nizu redova i prvo proveri da li u nekom baferu ima nesto. 
		Ako ima, prodji kroz niz soketa i vidi da li ima aktivni soket kroz koji neki klijent ceka na tom redu.
		Ako ima, skini poruku sa bafera i posalji je klijentu.
	*/
	while (1) {
		if (type == 1) {
			for (int i = 0; i < queue->size; i += 2) {
				if (queue->buffer[i].count > 0) {
					char *recvbuf = (char *)malloc(sizeof(char) * queue->buffer[i].size+1);
					memset(recvbuf, 0, queue->buffer[i].size);
					int iResult = sendOnSocket(&queue->buffer[i], serviceSocket, recvbuf, NULL);
					if (iResult != 0) {
						break;  //////// STA OVDE ?????
					}
					//free(recvbuf);
				}
			}
		}
		else {
			for (int i = 1; i < queue->size; i+=2) {
				if (queue->buffer[i].count > 0) {
					for (int j = 0; j < socketArray->size; j++) {
						if (socketArray->sockets[j].bufferName != NULL && socketArray->sockets[j].socket != INVALID_SOCKET) {
							if (strcmp(socketArray->sockets[j].bufferName, queue->buffer[i].name) == 0) {
								char *recvbuf = (char *)malloc(sizeof(char) * queue->buffer[i].size);
								memset(recvbuf, 0, queue->buffer[i].size);
								int iResult = sendOnSocket(&queue->buffer[i], NULL, recvbuf, &socketArray->sockets[j]);
								
								if (iResult != 0) {
									closesocket(socketArray->sockets[j].socket);
									socketArray->sockets[j].socket = INVALID_SOCKET;
									socketArray->sockets[j].bufferName = NULL;
									break;  //////// STA OVDE ?????
								}
								//free(recvbuf);
							}
						}
					}

				}
			}
		}
	}

	return 0;
}

DWORD WINAPI ClientServerThread(LPVOID lpParam)
{

	/*
		Preuzimanje niza soketa, niza tredova i reda napravljenih u main() funkciji servera.
	*/
	CSParams csParams = *(CSParams*)lpParam;
	SocketArray *socketArray = csParams.socketArray;
	ThreadArray *threadArray = csParams.threadArray;
	Queue *queue = csParams.queue;
	// Socket used for listening for new clients 
	SOCKET listenSocket = INVALID_SOCKET;
	// variable used to store function return value
	int iResult;
	// Buffer used for storing incoming data
	//char recvbuf[DEFAULT_BUFLEN];



	// Prepare address information structures
	addrinfo *resultingAddress = NULL;
	addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;       // IPv4 address
	hints.ai_socktype = SOCK_STREAM; // Provide reliable data streaming
	hints.ai_protocol = IPPROTO_TCP; // Use TCP protocol
	hints.ai_flags = AI_PASSIVE;     // 



	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &resultingAddress);
	if (iResult != 0)
	{
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	// Create a SOCKET for connecting to server
	listenSocket = socket(AF_INET,      // IPv4 address famly
		SOCK_STREAM,  // stream socket
		IPPROTO_TCP); // TCP

	if (listenSocket == INVALID_SOCKET)
	{
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(resultingAddress);
		WSACleanup();
		return 1;
	}

	// SET LISTEN SOCKET TO NON BLOCKING
	unsigned long int nonBlockingMode = 1;
	iResult = ioctlsocket(listenSocket, FIONBIO, &nonBlockingMode);

	// Setup the TCP listening socket - bind port number and local address 
	// to socket
	iResult = bind(listenSocket, resultingAddress->ai_addr, (int)resultingAddress->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(resultingAddress);
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}
	printf("\nLISTEN SOCKET: %d\n", listenSocket);
	// Since we don't need resultingAddress any more, free it
	freeaddrinfo(resultingAddress);

	// Set listenSocket in listening mode
	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(listenSocket);
		WSACleanup();
		return 1;
	}

	printf("Server for clients initialized, waiting for clients.\n");

	do
	{
		// Wait for clients and accept client connections.
		// Returning value is acceptedSocket used for further
		// Client<->Server communication. This version of
		// server will handle only one client.


		for (int i = 1; i < socketArray->size; i++) {

			if (socketArray->sockets[i].socket == INVALID_SOCKET) {
				// postavi accepted socket u non blocking mode i radi selekt
				iResult = ioctlsocket(socketArray->sockets[i].socket, FIONBIO, &nonBlockingMode);
				FD_SET set;
				timeval timeVal;

				do {
					iResult = 0;
					FD_ZERO(&set);
					// Add socket we will wait to read from
					FD_SET(listenSocket, &set);

					// Set timeouts to zero since we want select to return
					// instantaneously
					timeVal.tv_sec = 0;
					timeVal.tv_usec = 0;
					iResult = select(0 /* ignored */, &set, NULL, NULL, &timeVal);

					// lets check if there was an error during select
					if (iResult == SOCKET_ERROR)
					{
						fprintf(stderr, "select failed with error: %ld\n", WSAGetLastError());
						//return -1; //error code: -1
						break;
					}

					// now, lets check if there are any sockets ready
					if (iResult == 0)
					{
						//printf("\n SPAVAM! ");
						// there are no ready sockets, sleep for a while and check again
						Sleep(200);
					}

				} while (iResult == 0);
				
				if (socketArray->sockets[i].socket != INVALID_SOCKET)
					break;
				
					
 				socketArray->sockets[i].socket = accept(listenSocket, NULL, NULL);

				if (socketArray->sockets[i].socket == INVALID_SOCKET)
				{
					printf("accept failed with error: %d\n", WSAGetLastError());
					closesocket(listenSocket);
					//WSACleanup();
					//return 1;
					break;
				}
				printf("\n KLIJENT JE PRIHVACEN!\n");

				PushParams params;
				params.queue = queue;
				params.socket = &socketArray->sockets[i];
				params.socketArray = socketArray;
				// pronadji prvi slobodan tred i kreiraj novi
				for (int i = 4; i < threadArray->size; i++) {
					if (threadArray->threads[i] == NULL) {
						DWORD threadID;
						threadArray->threads[i] = CreateThread(NULL, 0, &PushInService, &params, 0, &threadID);
						break;
					}
				}
				/*
				// shutdown the connection since we're done
				iResult = shutdown(socketArray->sockets[i], SD_SEND);
				if (iResult == SOCKET_ERROR)
				{
					printf("shutdown failed with error: %d\n", WSAGetLastError());
					closesocket(socketArray->sockets[i]);
					return 1;
				}
				*/
				break;
			}

		}




	} while (1);
	// TODO: CLEANUP SVIH RESURSA!


	// cleanup
	//closesocket(listenSocket);
	//WSACleanup();




	return 0;
}



DWORD WINAPI GarbageCollector(LPVOID lpParam)
{
	
	CSParams *threadArray = (CSParams *)lpParam;
	DWORD result;
	for (int i = 3; i < threadArray->threadArray->size; i++) {
		result = WaitForSingleObject(threadArray->threadArray->threads[i], 0);

		// Ako je thread zavrsen onda je ovo u pitanju
		if (result == WAIT_OBJECT_0) {
			CloseHandle(threadArray->threadArray->threads[i]);
			printf("Unistavam nit!\n");
			threadArray->threadArray->threads[i] = NULL;
			threadArray->threadArray->count--;
		}
	}
	Sleep(200);
	return 0;
}


DWORD WINAPI ReceiveFromService(LPVOID lpParam)
{
	PPSParams* ppsParams = (PPSParams*)lpParam;
	Queue *queue = ppsParams->queue;
	SocketArray *socketArray = ppsParams->socketArray;
	int type = ppsParams->type;
	SOCKET *serviceSocket = ppsParams->serviceSocket;
	int iResult = 0;
	while (1) {
		
		iResult = select(serviceSocket, 0);
		if (iResult == 1) {
			char recvbuf[DEFAULT_BUFLEN];
			iResult = receiveServerFromServer(serviceSocket, recvbuf);

			if (iResult == 0) {
				pushInBuffer(recvbuf, NULL, queue, serviceSocket, socketArray);
			}
		}
	
		Sleep(50);
	}

	
	return 0;
}


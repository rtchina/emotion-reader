// ConsoleApplication3.cpp : Define o ponto de entrada para a aplicação de console.

#include "stdafx.h"

#include "thinkgear.h"
#include "NSK_Algo.h"
#include <thread>
#include <fstream>
#include <iostream>
#include <windows.h>
#include <string>

#ifdef _WIN64
#define ALGO_SDK_DLL			L"AlgoSdkDll64.dll"
#else
#define ALGO_SDK_DLL			L"AlgoSdkDll.dll"
#endif

#define NSK_ALGO_CDECL(ret, func, args)		typedef ret (__cdecl *func##_Dll) args; func##_Dll func##Addr = NULL; char func##Str[] = #func;

using std::string;

//Declaracao das funcoes do sdk do neurosky
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_Init, (eNSK_ALGO_TYPE type, const NS_STR dataPath));
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_Uninit, (NS_VOID));
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_DataStream, (eNSK_ALGO_DATA_TYPE type, NS_INT16 *data, NS_INT dataLenght));
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_RegisterCallback, (NskAlgo_Callback cbFunc, NS_VOID *userData));
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_Start, (NS_BOOL bBaseline));
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_Pause, (NS_VOID));
NSK_ALGO_CDECL(eNSK_ALGO_RET, NSK_ALGO_Stop, (NS_VOID));

//Declaracao de variaveis globais
HWND hWnd;					//handle da janela de console
char comPortName[5];		//nome da porta COM
int dllVersion = 0;			//versao da biblioteca usada
int errCode = 0;			//codigo de retorno das funcoes
int connectionId = -1;		//identificacao da conexao
int packetsRead = 0;		//pacotes lidos
DWORD dwThreadId = -1;		//identificacao da thread de leitura
HANDLE threadHandle = NULL;	//handle da thread de leitura
bool connected;				//status de conexao
int algos = 0;				//algoritmos selecionados

string isadoraName;			//Nome do programa a rodar no isadora
string comPort;				//Nome da porta COM
char isaName[256];			//Nome da janela a rodar

//Faixas para a medicao de atencao
int med_faixa1 = 20, med_faixa2 = 40, med_faixa3 = 60, med_faixa4 = 80;
int med = 0, animation = 0;

//Valores para detectar emocao
int valence = 0;
int emotion = 0;

float delta = 0, theta = 0, alpha = 0, beta = 0, gamma = 0;					//Valores salvos
float deltanew = 0, thetanew = 0, alphanew = 0, betanew = 0, gammanew = 0;	//Valores lidos
float alphaN = 0, betaN = 0, gammaN = 0;									//Valores normalizados

//Leituras de 5 segundos consecutivos - true (happy), false (sad)
bool values[5] = {false, false, false, false, false};

//Tempo dentro da janela de 5 segundos
int timeWindow = 0;

//Definicao do teclado virtual
INPUT ip;
//Variavel para nome de janelas
CHAR wnd_title[256];
//Handle da janela atual
HWND currWnd;

/*
Declaracao de funcoes do sdk
-AlgoSdkCallback = callback da leitura. Faz o processamento do sinal lido
-getFuncAddrs = cria as funcoes do sdk algo
-*getFuncAddr = retorna o * das funcoes
*/
static void AlgoSdkCallback(sNSK_ALGO_CB_PARAM param);
static bool getFuncAddrs(HINSTANCE hinstLib, HWND hWnd);
static void *getFuncAddr(HINSTANCE hinstLib, HWND hwnd, char *funcName, bool *bError);
static void ThreadReadPacket(LPVOID lpdwThreadParam);

/*
Declara as funcoes gerais
*/
void wait();
void disconnectGear();
void connectGear();
void classifyState();
void happinessLevelCounter();
void emotionDetection();
void sendKey(int em);

/*
	Cria as funcoes do sdk algo
*/
static bool getFuncAddrs(HINSTANCE hinstLib, HWND hWnd) {
	bool bError;

	NSK_ALGO_InitAddr = (NSK_ALGO_Init_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_InitStr, &bError);
	NSK_ALGO_UninitAddr = (NSK_ALGO_Uninit_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_UninitStr, &bError);
	NSK_ALGO_DataStreamAddr = (NSK_ALGO_DataStream_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_DataStreamStr, &bError);
	NSK_ALGO_RegisterCallbackAddr = (NSK_ALGO_RegisterCallback_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_RegisterCallbackStr, &bError);
	NSK_ALGO_StartAddr = (NSK_ALGO_Start_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_StartStr, &bError);
	NSK_ALGO_PauseAddr = (NSK_ALGO_Pause_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_PauseStr, &bError);
	NSK_ALGO_StopAddr = (NSK_ALGO_Stop_Dll)getFuncAddr(hinstLib, hWnd, NSK_ALGO_StopStr, &bError);

	return bError;
}

/*
	Retorna o * das funcoes
*/
static void *getFuncAddr(HINSTANCE hinstLib, HWND hwnd, char *funcName, bool *bError) {
	void *funcPtr = (void*)GetProcAddress(hinstLib, funcName);
	*bError = true;
	if (funcPtr == NULL) {
		*bError = false;
	}
	return funcPtr;
}

/*
	Limpa saidas para encerrar o programa
*/
void wait() {
	printf("\n");
	printf("Feche esta janela\n");
	fflush(stdout);
	getc(stdin);
}


/*
	Programa principal. Define as variaveis e inicia a thread de leitura
*/
int main()
{
	//Leitura do arquivo de configuracao config.ini
	std::ifstream infile;
	infile.open("config.ini");
	if (infile) {
		getline(infile, comPort);		//Salva a linha na string
		getline(infile, isadoraName);	//Salva a linha na string

		//Imprime os valores salvos para conferencia
		std::cout << comPort << "\n";
		std::cout << isadoraName << "\n";

		//Passa o nome da porta COM para o formato adequado
		strcpy(comPortName, comPort.c_str());
		//Passa o nome do programa isadora para o formato adequado
		strcpy(isaName, isadoraName.c_str());

		//Encerra leitura do arquivo de configuracao
		infile.close();
	}
	else {
		printf("Erro de leitura\n");
	}
	printf("Configurado\n");
		
	//Cria o evento de teclado
	ip.type = INPUT_KEYBOARD;	//Define teclado
	ip.ki.wScan = 0;			//Varredura de codigo por tecla
	ip.ki.time = 0;				//Tempo = 0	
	ip.ki.dwExtraInfo = 0;		//Informacoes extras

	currWnd = GetForegroundWindow(); // get handle of currently active window

	//Inicia desconectado
	connected = false;
	//Carrega a biblioteca ALGO_SDK_DLL
	HINSTANCE hinstLib = LoadLibrary(ALGO_SDK_DLL);
	//Cria um handler para a consoleWindow
	HWND hWnd = GetConsoleWindow();

	//Falha em carregar a biblioteca	
	if (hinstLib == NULL) {
		printf("Falhou em carregar a biblioteca\n");
	}
	//Sucesso
	else {
		//Cria as funcoes do sdk
		bool funcAddrs = getFuncAddrs(hinstLib, hWnd);
		//Se nao der certo
		if (funcAddrs == false) {
			FreeLibrary(hinstLib);
			printf("Nao carregou funcoes\n");
			wait();
		}
		//Sucesso
		else {
			printf("Funcoes carregadas\n");
		}
	}
	//Seleciona os algoritmos usados: BandPower e Meditacao
	algos |= NSK_ALGO_TYPE_BP;
	algos |= NSK_ALGO_TYPE_MED;

	//Enquanto nao conectar, tenta de novo
	while (!connected) {
		connectGear();
		_sleep(1000);
	}

	//Cria a thread de leitura
	if ((threadHandle = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&ThreadReadPacket, NULL, 0, &dwThreadId)) == NULL) {
		MessageBox(hWnd, L"Falha ao criar a thread de leitura de pacotes", L"Error", MB_OK);
	}
	//Define o callback para o algo
	eNSK_ALGO_RET ret = (NSK_ALGO_RegisterCallbackAddr)(&AlgoSdkCallback, hWnd);

	//Inicia os algoritmos
	ret = (NSK_ALGO_StartAddr)(NS_FALSE);

	//Loop infinito
	if (NSK_ALGO_RET_SUCCESS == ret) {
		WaitForSingleObject(threadHandle, INFINITE);
	}

	//Desconecta capacete
	disconnectGear();

	//Fim de programa
	wait();
	return(EXIT_SUCCESS);
}

/*
	Desconecta o capacete e libera a conexao
*/
void disconnectGear() {
	//Define nao-conectado
	connected = false;
	if (connectionId >= 0) {
		//Para algoritmos correntes
		eNSK_ALGO_RET NSK_ALGO_Uninit();
		//Desconecta
		TG_Disconnect(connectionId);
		//Libera a conexao
		TG_FreeConnection(connectionId);
	}
}

/*
	Conecta o capacete 
*/
void connectGear() {
	//Desconecta qualquer conexao remanescente
	disconnectGear();
	//Define a connectionId
	connectionId = TG_GetNewConnectionId();
	//Se nao conseguiu um Id
	if (connectionId < 0) {
		printf("Falha em conseguir um connectionId");
		wait();
		exit(0);
	}
	//Se conseguiu um Id
	else {
		//Tenta conectar com essa Id e a porta definida. Baud 115200
		int err = TG_Connect(connectionId,
			comPortName,
			TG_BAUD_115200,
			TG_STREAM_PACKETS);
		//Se a conexao falhar
		if (err < 0) {
			printf("Conexao Falhou... Tentando novamente\n");
		}
		//Se conseguir conectar
		else {
			printf("Capacete conectado na porta %s\n", comPortName);
			//Inicia os algoritmos escolhidos
			int ret = (NSK_ALGO_InitAddr)((eNSK_ALGO_TYPE)algos, "C:\\Neurosky\\Documents\\log.txt");
			//Inicia o processo
			ret = (NSK_ALGO_StartAddr)(NS_FALSE);
			connected = true;
		}
	}	
}

/*
	Decide o nivel de felicidade da janela de 5 segundos
*/
static void happinessLevelCounter() {
	//Contador de estados felizes
	int happyCounter = 0;
	//Varre a janela de 5 segundos
	for (int i = 0; i < 5; i++) {
		//Conta os valores true
		if (values[i]) {
			happyCounter++;
			values[i] = false;
		}
	}
	//Atribui o valor de valencia
	valence = happyCounter;

	//Detecta a emocao com base na valencia e excitacao
	emotionDetection();
}

/*
	Classifica se o estado atual esta em feliz ou triste
*/
static void classifyState() {
	//Verifica a posicao num plano xyz (alphaN, betaN, gammaN)
	//Valores obtidos por treinamento de samples em SVM simplificada
	if (gammaN > (0.5*betaN + 0.55)) {
		values[timeWindow] = true;
	}
	else {
		values[timeWindow] = false;
	}
	//Incrementa a timeWindow
	timeWindow++;
	//Se preencher os 5 segundos verifica felicidade e zera o contador
	if (timeWindow >= 5) {
		timeWindow = 0;
		happinessLevelCounter();
	}
}

/*
	Normaliza os valores de alpha, beta e gamma
*/
static void normalize() {
	//Resultados entre 0 e 1. Valores de normalizacao obtidos por observacao de exemplos
	alphaN = alpha / 20.0;
	betaN  = beta / 15.0;
	gammaN = (gamma + 10) / 20.0;

	//Se algum dos valores estiver fora da escala 0-1 entao retorna
	if (alphaN < 0 || alphaN > 1) {
		alphaN = -1;
		return;
	}

	if (betaN < 0 || betaN > 1) {
		betaN = -1;
		return;
	}
	if (gammaN < 0 || gammaN > 1) {
		gammaN = -1;
		return;
	}

	//printf("%.3f %.3f %.3f;\n", alphaN, betaN, gammaN);
	classifyState();
}

/*
	Thread de leitura de pacotes do capacete
*/
static void ThreadReadPacket(LPVOID lpdwThreadParam) {
	int rawCount = 0;
	short rawData[512] = { 0 };

	//Loop infinito
	while (true) {
		if (connected) {
			//Le um pacote da conexao
			packetsRead = TG_ReadPackets(connectionId, 1);

			//Se conseguir ler 1 pacote
			if (packetsRead == 1) {
				//Se o pacote contem um novo valor raw
				if (TG_GetValueStatus(connectionId, TG_DATA_RAW) != 0) {
					//Adiciona o valor no conjunto de dados a ser analisado
					rawData[rawCount++] = (short)TG_GetValue(connectionId, TG_DATA_RAW);
					//Quando encher o conjunto com 512 amostras (aprox. 1 segundo)
					if (rawCount == 512) {
						//Envia o conjunto de dados para o algoritmo e reseta contador
						NSK_ALGO_DataStreamAddr(NSK_ALGO_DATA_TYPE_EEG, rawData, rawCount);
						rawCount = 0;
					}
				}
				//Se estiver com sinal baixo
				if (TG_GetValueStatus(connectionId, TG_DATA_POOR_SIGNAL) != 0) {
					short pq = (short)TG_GetValue(connectionId, TG_DATA_POOR_SIGNAL);
					(NSK_ALGO_DataStreamAddr)(NSK_ALGO_DATA_TYPE_PQ, &pq, 1);
				}
				//Analisa dados de atencao
				if (TG_GetValueStatus(connectionId, TG_DATA_ATTENTION) != 0) {
					short att = (short)TG_GetValue(connectionId, TG_DATA_ATTENTION);
					(NSK_ALGO_DataStreamAddr)(NSK_ALGO_DATA_TYPE_ATT, &att, 1);
				}
				//Analisa dados de meditacao
				if (TG_GetValueStatus(connectionId, TG_DATA_MEDITATION) != 0) {
					short med = (short)TG_GetValue(connectionId, TG_DATA_MEDITATION);
					(NSK_ALGO_DataStreamAddr)(NSK_ALGO_DATA_TYPE_MED, &med, 1);
				}
			}
		}
		//Se nao estiver conectado espera 1s e tenta conectar novamente
		else {
			_sleep(1000);
			connectGear();
			if (connected) {
				printf("Reading signal\n");
			}
		}
	}
}

/*
	Callback do ALGOSDK
*/
static void AlgoSdkCallback(sNSK_ALGO_CB_PARAM param) {
	switch (param.cbType) {
	//Para o nivel do sinal - se estiver qualquer coisa alem de bom, nao aceita 
	case NSK_ALGO_CB_TYPE_SIGNAL_LEVEL:
	{
		eNSK_ALGO_SIGNAL_QUALITY sq = (eNSK_ALGO_SIGNAL_QUALITY)param.param.sq;
		switch (sq) {
		case NSK_ALGO_SQ_GOOD:
			break;
		case NSK_ALGO_SQ_MEDIUM:
			delta = 0;
			theta = 0;
			alpha = 0;
			beta = 0;
			gamma = 0;
			sendKey(0);
			break;
		default:
			sendKey(0);
			disconnectGear();
			connected = false;
			
			printf("Lost signal\n");
			break;
		}
	}
	break;
	//Para o tipo de algoritmo
	case NSK_ALGO_CB_TYPE_ALGO:
	{
		switch (param.param.index.type) {
		//Meditacao - divide o resultado em faixas para classificar arousal
		case NSK_ALGO_TYPE_MED:
		{
			med = param.param.index.value.group.med_index;
			if (med <= med_faixa1) {
				animation = -2;
				break;
			}else if (med <= med_faixa2) {
				animation = -1;
				break;
			}
			else if (med <= med_faixa3) {
				animation = 0;
				break;
			}
			else if (med <= med_faixa4) {
				animation = 1;
				break;
			}
			else {
				animation = 2;
				break;
			}
			break;
		}
		//BandPower - le o valor de cada banda do sinal
		case NSK_ALGO_TYPE_BP:
		{
			deltanew = param.param.index.value.group.bp_index.delta_power;
			thetanew = param.param.index.value.group.bp_index.theta_power;
			alphanew = param.param.index.value.group.bp_index.alpha_power;
			betanew  = param.param.index.value.group.bp_index.beta_power;
			gammanew = param.param.index.value.group.bp_index.gamma_power;

			if (fabs(deltanew) > 0.1 || fabs(thetanew) > 0.1 || fabs(alphanew) > 0.1 || fabs(betanew) > 0.1 || fabs(gammanew) > 0.1) {
				delta = deltanew;
				theta = thetanew;
				alpha = alphanew;
				beta = betanew;
				gamma = gammanew;
			}
			
			//Normaliza o sinal
			normalize();
			break;
		}
		}
		break;
	}
	}
	
}

/*
	Detecta a emocao com base na excitacao(animacao) e valencia(felicidade)
*/
void emotionDetection() {
	//Animacao: -2 -1 0 1 2
	//Valencia: 0 1 2 - sad | 3 4 5 - happy

	//Resultados:
	//  0 - neutro
	//	1 - surrealismo
	//	2 - minimalismo
	//	3 - neoclassico
	//	4 - grotesco
	//	5 - impressionismo
	//	6 - expressionismo
	//	7 - abstrato
	//	8 - psicodelico

	if(animation < 0) {
		if(valence == 0 || valence == 1) {
			printf("AFLITO, FATIGADO - SURREALISMO\n");
			emotion = 1;
		}
		else if (valence == 2 || valence == 3) {
			printf("SONOLENTO, ENTEDIADO - MINIMALISMO\n");
			emotion = 2;
		}
		else if (valence == 4 || valence == 5) {
			printf("CALMO, RELAXADO - NEOCLASSICO\n");
			emotion = 3;
		}
	}
	else if (animation == 0) {
		if (valence == 0 || valence == 1) {
			printf("TRISTE, DESAPONTADO - GROTESCO\n");
			emotion = 4;
		}
		else if (valence == 2 || valence == 3) {
			printf("NEUTRO\n");
			emotion = 0;
		}
		else if (valence == 4 || valence == 5) {
			printf("FELIZ, SATISFEITO - IMPRESSIONISMO\n");
			emotion = 5;
		}
	}
	else{
		if (valence == 0 || valence == 1) {
			printf("IRRITADO, TENSO - EXPRESSIONISMO\n");
			emotion = 6;
		}
		else if (valence == 2 || valence == 3) {
			printf("SURPRESO, EXALTADO - ABSTRATO\n");
			emotion = 7;
		}
		else if (valence == 4 || valence == 5) {
			printf("EXTASIADO, ENTUSIASMADO - PSICODELICO\n");
			emotion = 8;
		}	
	}
	sendKey(emotion);
}

/*
	Envia o comando numerico via teclado
*/
static void sendKey(int em) {
	currWnd = GetForegroundWindow();
	GetWindowTextA(currWnd, wnd_title, sizeof(wnd_title));
	//printf("%s\n", wnd_title);

	//if (strcmp(wnd_title, isaName) == 0) {
	char *output = NULL;
	output = strstr(wnd_title, isaName);
	if (output) {
		ip.ki.wVk = 0x30 + em;

		//Aperta uma tecla numerica
		ip.ki.dwFlags = 0; //0 para apertar
		SendInput(1, &ip, sizeof(INPUT));

		//Solta a tecla
		ip.ki.dwFlags = KEYEVENTF_KEYUP; //keyeventf_keyup para soltar
		SendInput(1, &ip, sizeof(INPUT));
	}
}
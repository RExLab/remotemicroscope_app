

#include "timer/timer.h"
#include "_config_cpu_.h"
#include "uart/uart.h"
#include "modbus/modbus_master.h"
#include <nan.h>
#include "app.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <v8.h>
#include <string.h>
#define SLEEP_TIME 10000
#define TIMEOUT 200

using namespace v8;
static pthread_t id_thread;
static int waitResponse = pdFALSE; // Testas com inicio pdFalse
static tCommand cmd;
static u16 regs[120]; // registrador de trabalho para troca de dados com os multimetros
tControl control;

int modbus_Init(void) {
    //fprintf(flog, "Abrindo UART %s"CMD_TERMINATOR, COM_PORT);
#if (LOG_MODBUS == pdON)
    printf("Abrindo UART %s"CMD_TERMINATOR, COM_PORT);
#endif
    if (uart_Init(COM_PORT, MODBUS_BAUDRATE) == pdFAIL) {
        //fprintf(flog, "Erro ao abrir a porta UART"CMD_TERMINATOR);
        //fprintf(flog, "    Verifique se essa porta n�o esteja sendo usada por outro programa,"CMD_TERMINATOR);
        //fprintf(flog, "    ou se o usu�rio tem permiss�o para usar essa porta: chmod a+rw %s"CMD_TERMINATOR, COM_PORT);
#if (LOG_MODBUS == pdON)
        printf("Erro ao abrir a porta UART"CMD_TERMINATOR);
        printf("    Verifique se essa porta n�o esteja sendo usada por outro programa,"CMD_TERMINATOR);
        printf("    ou se o usu�rio tem permiss�o para usar essa porta: chmod a+rw %s"CMD_TERMINATOR, COM_PORT);
#endif
        return pdFAIL;
    }

    //fprintf(flog, "Port UART %s aberto com sucesso"CMD_TERMINATOR, COM_PORT);
#if (LOG_MODBUS == pdON)
    printf("Port UART %s aberto com sucesso a %d bps"CMD_TERMINATOR, COM_PORT, MODBUS_BAUDRATE);
#endif
    modbus_MasterInit(uart_SendBuffer, uart_GetChar, uart_ClearBufferRx);
    modbus_MasterAppendTime(now, 3000);

    return pdPASS;
}



// para grava��o os valores dos registradores devem estar preenchidos
// ap�s a leitura os registradores estar�o preenchidos

// Envia um comando para o escravo, nesta mesma fun��o � feito o procedimento de erro de envio
// Uma vez enviado o comando com sucesso caprturar resposta no modbus_Process
//	BUSY: Ficar na espera da resposta
//  ERROR: Notificar  o erro e tomar procedimento cab�veis
//  OK para escrita: Nada, pois os valores dos registradores foram salvos no escravo com sucesso
//	OK para Leitura: Capturar os valores dos registradores lidos do escravo
// Parametros
// c: Tipo de comando para ser enviado ao escravo
// funcResponse: Ponteiro da fun��o para processar a resposta da comunica��o

void modbus_SendCommand(tCommand c) {
    if (waitResponse) return;

    cmd = c;
    // APONTA QUAIS REGISTRADORES A ACESSAR NO DISPOSITIVO
    // -----------------------------------------------------------------------------------------------------------------

    // Comando para ler os registradores: modelo, vers�o firmware e modo de opera��es dos reles
    tcmd typeCMD = readREGS;
    uint addrInit = 0;
    uint nRegs = 3;
    u16 value = 0;

    // Comando para ler que a��o o recurso de hardware est� fazendo agora
    if (cmd == cmdGET_ACTION) {
        addrInit = 0x10;
        nRegs = 1;

        // Comando para o status do experimento
    } else if (cmd == cmdGET_SLOT) {
        addrInit = 0x11;
        nRegs = 1;

        // Comando para pegar quantas amostras foram tiradas durante experimento. M�ixma 600 amostra = 10 minutos de experimento
    } else if (cmd == cmdGET_SWITCH) {
        addrInit = 0x12;
        nRegs = 1;

        // Comando para ajustar a grandeza da temperatura
    } else if (cmd == cmdSET_SLOT) {
        typeCMD = writeREG;
        addrInit = 0x13;
        nRegs = 1;
        value = (control.slot)&0xff;

        // Comando para dar in�cio ao experimento
    } else if (cmd == cmdSET_MOTOR) {
        typeCMD = writeREG;
        addrInit = 0x14;
        nRegs = 1;
        //value = control.tempType;
        // Comando para parar o experimento
    }


    // ENVIA O COMANDO AO DISPOSITIVO ESCRAVO
    // -----------------------------------------------------------------------------------------------------------------
    int ret;
    if (typeCMD == writeREG) {
#if (LOG_MODBUS == pdON)
        printf("modbus WriteReg [cmd %d] [slave %d] [reg 0x%x] [value 0x%x]"CMD_TERMINATOR, cmd, control.rhID, addrInit, value);
#endif
        ret = modbus_MasterWriteRegister(control.rhID, addrInit, value);
    } else if (typeCMD == writeREGS) {
#if (LOG_MODBUS == pdON)
        printf("modbus WriteRegs [cmd %d] [slave %d] [reg 0x%x] [len %d]"CMD_TERMINATOR, cmd, control.rhID, addrInit, nRegs);
#endif
        ret = modbus_MasterWriteRegisters(control.rhID, addrInit, nRegs, regs);
    } else {
#if (LOG_MODBUS == pdON)
        printf("modbus ReadRegs [cmd %d] [slave %d] [reg 0x%x] [len %d]"CMD_TERMINATOR, cmd, control.rhID, addrInit, nRegs);
#endif
        ret = modbus_MasterReadRegisters(control.rhID, addrInit, nRegs, regs);
    }


    // se foi enviado com sucesso ficaremos na espera da resposta do recurso de hardware
    if (ret == pdPASS) waitResponse = pdTRUE;
#if (LOG_MODBUS == pdON)
        //else fprintf(flog, "modbus err[%d] send querie"CMD_TERMINATOR, modbus_MasterReadStatus());
    else printf("modbus err[%d] SEND querie"CMD_TERMINATOR, modbus_MasterReadStatus());
#endif

    return;
}

void init_control_tad() {
    control.rhID = 1; // Sinaliza que o endere�o do RH no modbus � 1
    control.getInfo = 0; // sinaliza para pegar as informa��es do RH
    control.action = 0;
    control.status = 0;
    control.getSlot = 0;
    control.setSlot = 0;
    control.exit = 0;
    control.slot = 1;
    waitResponse = pdFALSE;
    memset(control.rhModel, '\0', __STRINGSIZE__);
    memset(control.rhFirmware, '\0', __STRINGSIZE__);

    return;

}

// processo do modbus.
//	Neste processo gerencia os envios de comandos para o recurso de hardware e fica no aguardo de sua resposta
//	Atualiza as variaveis do sistema de acordo com a resposta do recurso de hardware.

void * modbus_Process(void * params) {

    int first = 0;
    while (control.exit == 0) {
        if (first) {
            usleep(SLEEP_TIME);
        }
        first = 1;
        modbus_MasterProcess();

        // Gerenciador de envio de comandos
        // se n�o estamos esperando a resposta do SendCommand vamos analisar o pr�ximo comando a ser enviado
        if (!waitResponse) {
            // checa se � para pegar as informa��es do RH
            if (control.getSlot) {
                modbus_SendCommand(cmdGET_SLOT);

            } else if (control.setSlot) {
                modbus_SendCommand(cmdSET_SLOT);

            }
            continue;
        }


        int ret = modbus_MasterReadStatus();
        //	BUSY: Ficar na espera da resposta
        //  ERROR: Notificar  o erro e tomar procedimento cab�veis
        //  OK para escrita: Nada, pois os valores dos registradores foram salvos no escravo com sucesso
        //	OK para Leitura: Capturar os valores dos registradores lidos do escravo

        // se ainda est� ocupado n�o faz nada

        if (ret == errMODBUS_BUSY)continue;
        waitResponse = pdFALSE;
        // se aconteceu algum erro
        if (ret < 0) {
#if (LOG_MODBUS == pdON)
            printf("modbus err[%d] WAIT response "CMD_TERMINATOR, ret);
#endif
            control.stsCom = modbus_MasterReadException();
            // modbusILLEGAL_FUNCTION: O multimetro recebeu uma fun��o que n�o foi implementada ou n�o foi habilitada.
            // modbusILLEGAL_DATA_ADDRESS: O multimetro precisou acessar um endere�o inexistente.
            // modbusILLEGAL_DATA_VALUE: O valor contido no campo de dado n�o � permitido pelo multimetro. Isto indica uma falta de informa��es na estrutura do campo de dados.
            // modbusSLAVE_DEVICE_FAILURE: Um irrecuper�vel erro ocorreu enquanto o multimetro estava tentando executar a a��o solicitada.
            continue;
        }

        control.stsCom = 5; // sinaliza que a conex�o foi feita com sucesso

        // ATUALIZA VARS QUANDO A COMUNICA��O FOI FEITA COM SUCESSO
        // -----------------------------------------------------------------------------------------------------------------

        // Comando para ler os registradores: modelo e vers�o firmware do RH
        if (cmd == cmdSET_SLOT) {
            printf("Slot %i"CMD_TERMINATOR, control.slot);
            control.setSlot = 0;

        } else if (cmd == cmdGET_INFO) {
#if (LOG_MODBUS == pdON)
            printf("model %c%c%c%c"CMD_TERMINATOR, (regs[0] & 0xff), (regs[0] >> 8), (regs[1] & 0xff), (regs[1] >> 8));
            printf("firware %c.%c"CMD_TERMINATOR, (regs[2] & 0xff), (regs[2] >> 8));
#endif
            control.rhModel[0] = (regs[0] & 0xff);
            control.rhModel[1] = (regs[0] >> 8);
            control.rhModel[2] = (regs[1] & 0xff);
            control.rhModel[3] = (regs[1] >> 8);
            control.rhModel[4] = 0;
            control.rhFirmware[0] = (regs[2] & 0xff);
            control.rhFirmware[1] = (regs[2] >> 8);
            control.rhFirmware[2] = 0;

            control.getInfo = 0; // sinalizo para n�o pegar mais informa��es

            // comando para ajuste dos reles, vamos sinalizar para n�o enviar mais comandos
        } else if (cmd == cmdGET_ACTION) {
            control.action = regs[0];
            control.getAction = 0; // sinalizo para n�o pegar mais a��o

            // Comando para o status do experimento
        } else if (cmd == cmdGET_SLOT) {
            control.slot = regs[0];
            control.getSlot = 0;
            printf("Slot %i "CMD_TERMINATOR, regs[0]);
        }
    }
    printf("Saindo da thread\n");
    uart_Close();
    return NULL;

}

NAN_METHOD(Update) {
    NanScope();
    if (args.Length() < 1) {
        NanThrowTypeError("Wrong number of arguments");
        NanReturnUndefined();
    }

    if (!args[0]->IsNumber()) {
        NanThrowTypeError("Wrong arguments");
        NanReturnUndefined();

    }
    control.slot = args[0]->NumberValue();
    control.setSlot = 1;
    int timeout = TIMEOUT;
    while (control.setSlot) {
        usleep(SLEEP_TIME);
        timeout--;
        if (timeout < 0) {
            control.setSlot = 0;
            NanThrowError("In function setSlot: Modbus Timeout");
        }
    }
    NanReturnValue(NanNew(1));

}

NAN_METHOD(GetSlot) {
    NanScope();
    control.getSlot = 1;
    int timeout = TIMEOUT;
    while (control.getSlot) {
        usleep(SLEEP_TIME);
        timeout--;
        if (timeout < 0) {
            control.getSlot = 0;
            NanThrowError("In function setSlot: Modbus Timeout");
        }
    }
    NanReturnValue(NanNew(control.slot));

}

NAN_METHOD(Setup) {
    NanScope();
    FILE *flog = fopen("msip.log", "w");

    if (modbus_Init() == pdFAIL) {
        fprintf(flog, "Erro ao abrir a porta UART"CMD_TERMINATOR);
        fprintf(flog, "Verifique se essa porta n�o esteja sendo usada por outro programa,"CMD_TERMINATOR);
        fprintf(flog, "ou se o usu�rio tem permiss�o para usar essa porta: chmod a+rw %s"CMD_TERMINATOR, COM_PORT);
        NanReturnValue(NanNew("0"));

    }

    fclose(flog);

    NanReturnValue(NanNew("1"));

}

/*
NAN_METHOD(Run) {
    NanScope();

    init_control_tad();

    if (modbus_Init() == pdFAIL) {
        NanReturnValue(NanNew(0));
    }

    int rthr = pthread_create(&id_thread, NULL, modbus_Process, (void *) 0); // fica funcionando at� que receba um comando via WEB para sair		
    if (rthr) {
        NanReturnValue(NanNew(0));
    }

    NanReturnValue(NanNew(1));
}*/

NAN_METHOD(Run) {
    NanScope();

    init_control_tad();

    int rthr = pthread_create(&id_thread, NULL, modbus_Process, (void *) 0); // fica funcionando at� que receba um comando via WEB para sair		
    if (rthr) {
        printf("Unable to create thread void * modbus_Process");

    }

    NanReturnValue(NanNew("1"));

}

NAN_METHOD(Exit) {
    NanScope();
    control.exit = 1;
    NanReturnValue(NanNew(1));
}

void Init(Handle<Object> exports) { // cria fun��es para serem invocadas na aplica��o em nodejs
    exports->Set(NanNew("run"), NanNew<FunctionTemplate>(Run)->GetFunction());
    exports->Set(NanNew("setup"), NanNew<FunctionTemplate>(Setup)->GetFunction());

    exports->Set(NanNew("update"), NanNew<FunctionTemplate>(Update)->GetFunction());
    exports->Set(NanNew("exit"), NanNew<FunctionTemplate>(Exit)->GetFunction());
    exports->Set(NanNew("getslot"), NanNew<FunctionTemplate>(GetSlot)->GetFunction());
}


NODE_MODULE(panel, Init)


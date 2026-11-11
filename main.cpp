#include <windows.h>
#include <iostream>
#include <fstream>
#include <cstdint>

using namespace std;

//ESTRUTURA BMP
#pragma pack(push, 1)
struct CabecalhoArquivo {
    char     tipo[2];
    uint32_t tamArquivo;
    uint32_t reservado;
    uint32_t offset;
    uint32_t tamCab;
    int32_t  largura;
    int32_t  altura;
    uint16_t planos;
    uint16_t bpp;
    uint32_t compressao;
    uint32_t tamImagem;
    int32_t  resX;
    int32_t  resY;
    uint32_t clrUsadas;
    uint32_t clrImportantes;
};
#pragma pack(pop)

//Estrutura com ponteiro
struct ImagemInterna {
    int largura = 0;
    int altura = 0;
    int bpp = 0;
    unsigned char* pixels = nullptr; //Ponteiro para buffer de pixels
};

//LEITURA BMP
bool CarregarBMP(const string& caminho, ImagemInterna& img) {
    ifstream arq(caminho, ios::binary);
    if (!arq) {
        cerr << "Erro: Nao foi possivel abrir o arquivo BMP." << endl;
        return false;
    }

    CabecalhoArquivo cab;
    arq.read((char*)&cab, sizeof(cab));

    if (cab.tipo[0] != 'B' || cab.tipo[1] != 'M' || cab.compressao != 0) {
        cerr << "Erro: Formato BMP invalido ou comprimido." << endl;
        return false;
    }

    if (cab.bpp != 24 && cab.bpp != 1) {
        cerr << "Erro: Apenas BMPs de 24-bits ou 1-bit sao suportados. (BPP: " << cab.bpp << ")" << endl;
        return false;
    }

    img.largura = cab.largura;
    img.altura = (cab.altura < 0) ? -cab.altura : cab.altura;
    img.bpp = cab.bpp;

    arq.seekg(cab.offset, ios::beg);

    //Alocação da memória com base no tamanho necessário
    if (cab.bpp == 24) {
        int bytesPorLinha = img.largura * 3;
        int padding = (4 - (bytesPorLinha % 4)) % 4;
        int tamanhoTotal = bytesPorLinha * img.altura;

        //Aloca o bloco de memória diretamente na heap
        img.pixels = new unsigned char[tamanhoTotal];

        for (int y = 0; y < img.altura; y++) {
            int linhaReal = (cab.altura > 0) ? (img.altura - 1 - y) : y;
            arq.read((char*)&img.pixels[linhaReal * bytesPorLinha], bytesPorLinha);
            if (padding > 0) {
                arq.seekg(padding, ios::cur);
            }
        }
    }
    else if (cab.bpp == 1) {
        int bytesPorLinha = ((img.largura + 31) / 32) * 4;
        int tamanhoTotal = bytesPorLinha * img.altura;

        //Aloca o bloco de memória diretamente na heap
        img.pixels = new unsigned char[tamanhoTotal];

        for (int y = 0; y < img.altura; y++) {
            int linhaReal = (cab.altura > 0) ? (img.altura - 1 - y) : y;
            arq.read((char*)&img.pixels[linhaReal * bytesPorLinha], bytesPorLinha);
        }
    }

    arq.close();
    return true;
}

//Função para liberar a memória alocada (evita memory leak)
void LiberarImagem(ImagemInterna& img) {
    if (img.pixels != nullptr) {
        delete[] img.pixels;//Libera o bloco de memória alocado com new[]
        img.pixels = nullptr;
    }
}

//FUNÇÃO DE CONVERSÃO PARA OS 504 BYTES DO NOKIA 5110
bool ConverterPara504Bytes(const ImagemInterna& imgOrigem, unsigned char destino504[504]) {
    for (int i = 0; i < 504; i++) destino504[i] = 0;

    int largura = (imgOrigem.largura < 84) ? imgOrigem.largura : 84;
    int altura = (imgOrigem.altura < 48) ? imgOrigem.altura : 48;

    if (imgOrigem.bpp == 24) {
        int bytesPorLinha = imgOrigem.largura * 3;
        for (int y = 0; y < altura; y++) {
            int yBMP = altura - 1 - y;

            for (int x = 0; x < largura; x++) {
                int idxBMP = yBMP * bytesPorLinha + x * 3;
                unsigned char b = imgOrigem.pixels[idxBMP];
                unsigned char g = imgOrigem.pixels[idxBMP + 1];
                unsigned char r = imgOrigem.pixels[idxBMP + 2];
                unsigned char cinza = static_cast<unsigned char>(0.299 * r + 0.587 * g + 0.114 * b);

                if (cinza > 127) {
                    int blocoY = y / 8;
                    int bitPos = y % 8;
                    int indice504 = x + (blocoY * 84);
                    destino504[indice504] |= (1 << bitPos);
                }
            }
        }
    }
    else if (imgOrigem.bpp == 1) {
        int bytesPorLinha = ((imgOrigem.largura + 31) / 32) * 4;
        for (int y = 0; y < altura; y++) {
            int yBMP = altura - 1 - y;
            for (int x = 0; x < largura; x++) {
                int byteIdx = yBMP * bytesPorLinha + (x / 8);
                int bitIdx = 7 - (x % 8);
                unsigned char byteAtual = imgOrigem.pixels[byteIdx];
                int bitVal = (byteAtual >> bitIdx) & 1;

                if (bitVal == 1) {
                    int blocoY = y / 8;
                    int bitPos = y % 8;
                    int indice504 = x + (blocoY * 84);
                    destino504[indice504] |= (1 << bitPos);
                }
            }
        }
    }
    return true;
}

//API SERIAL
HANDLE AbrirPortaSerial(const string& porta) {
    HANDLE hCom = CreateFileA(porta.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom == INVALID_HANDLE_VALUE) {
        cerr << "Erro: Nao foi possivel abrir a porta " << porta << endl;
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hCom, &dcb);
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = EVENPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(hCom, &dcb);

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(hCom, &timeouts);

    return hCom;
}

//RETORNA RESPOTA DA PLACA
void RespostaServidor(char resposta) {
    switch (resposta) {
        case 0x41: cout << " [Retorno: ACK - Sucesso]" << endl; break;
        case 0x4E: cout << " [Retorno: NAK - Comando desconhecido/invalido]" << endl; break;
        case 0x4D: cout << " [Retorno: MND - Pode mandar os dados]" << endl; break;
        case 0x49: cout << " [Retorno: NID - Indice CID fora da faixa/invalido]" << endl; break;
        case 0x44: cout << " [Retorno: TOUT - Timeout na comunicacao]" << endl; break;
        default:   cout << " [Retorno desconhecido: 0x" << hex << (int)resposta << dec << "]" << endl; break;
    }
}

bool EnviarImagemSerial(HANDLE hCom, int idCubo, unsigned char* dadosImagem) {
    unsigned long bytesProcessados = 0;
    char resposta=0;

    //formata CID (0x80 a 0x9F)
    unsigned char cidFormatado = 0x80 + (idCubo & 0x1F);
    //envia comando SNC + CID
    unsigned char comandoSNC[2] = { 0xD3, cidFormatado };

    PurgeComm(hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);

    WriteFile(hCom, comandoSNC, 2, &bytesProcessados, NULL);

    //retorno do comando (Espera MND = 0x4D)
    ReadFile(hCom, &resposta, 1, &bytesProcessados, NULL);
    if (bytesProcessados == 0) {
        cout << "Erro: Timeout ao aguardar resposta do SNC." << endl;
        return false;
    }

    //placa retornou NID ou NAK, exibe o motivo exato
    if (resposta != 0x4D) {
        cout << "Erro no SNC:";
        RespostaServidor(resposta);
        return false;
    }

    //envia os 504 bytes da imagem
    WriteFile(hCom, dadosImagem, 504, &bytesProcessados, NULL);

    //confirmação final (Espera ACK = 0x41)
    ReadFile(hCom, &resposta, 1, &bytesProcessados, NULL);
    if (bytesProcessados == 0) {
        cout << "Erro: Timeout ao aguardar ACK final da imagem." << endl;
        return false;
    }

    if (resposta == 0x41) {
        cout << "Imagem gravada com sucesso no cubo " << idCubo << "!" << endl;
        return true;
    } else {
        cout << "Falha ao gravar imagem:";
        RespostaServidor(resposta);
        return false;
    }
}

bool AtivarImagemSerial(HANDLE hCom, int idCubo) {
    unsigned long bytesProcessados = 0;
    char resposta=0;

    unsigned char cidFormatado = 0x80 + (idCubo & 0x1F);
    unsigned char comandoATX[2] = { 0xD8, cidFormatado };

    PurgeComm(hCom, PURGE_RXCLEAR | PURGE_TXCLEAR);

    WriteFile(hCom, comandoATX, 2, &bytesProcessados, NULL);

    ReadFile(hCom, &resposta, 1, &bytesProcessados, NULL);
    if (bytesProcessados == 0) {
        cout << "Erro: Timeout ao ativar imagem." << endl;
        return false;
    }

    if (resposta == 0x41) {
        cout << "Display atualizado e exibindo a imagem (ATX OK)!" << endl;
        return true;
    } else {
        cout << "Erro ao ativar imagem (ATX):";
        RespostaServidor(resposta);
        return false;
    }
}


//APAGA IMAGEM DA TELA
bool ApagarDisplaySerial(HANDLE hCom) {
    unsigned long bytesProcessados = 0;
    char resposta = 0;

    unsigned char comandoCLR[2] = { 0xC3, 0xC3 };
    WriteFile(hCom, comandoCLR, 2, &bytesProcessados, NULL);

    ReadFile(hCom, &resposta, 1, &bytesProcessados, NULL);
    return (bytesProcessados > 0 && resposta == 0x41);
    if (bytesProcessados > 0 && resposta == 0x41) {
        cout << "[CLR] Display apagado com sucesso!" << endl;
        return true;
    } else {
        cout << "Erro ao apagar o display:";

        RespostaServidor(resposta);
        return false;
    }
}

int main() {
    string portaSerial = "COM7";

    cout << "Tentando abrir a porta serial " << portaSerial << "..." << endl;
    HANDLE hCom = AbrirPortaSerial(portaSerial);
    if (hCom == INVALID_HANDLE_VALUE) {
        return -1;
    }
    cout << "Porta serial aberta com sucesso!" << endl;

    ApagarDisplaySerial(hCom);

    //Lista de imagens
    const int totalImagens = 8;
    string listaImagens[totalImagens] = {
        "batman_nokia5110_dither_84x48.bmp",
        "batman_dithering_24bit.bmp",
        "superman_dithering_24bit.bmp",
        "wonderWoman_dithering_24bit.bmp",
        "flash_dithering_24bit.bmp",
        "aquaman_dithering_24bit.bmp",
        "greenLantern_dithering_24bit.bmp",
        "martianManHunter_dithering_24bit.bmp"
    };

    int tempoEsperaMs = 2000;//2 segundos por imagem
    int cuboDestino = 0;

    cout << "Iniciando exibicao automatica..." << endl;

    while (true) {
        for (int i = 0; i < totalImagens; i++) {
            cout << "\nCarregando: " << listaImagens[i] << endl;
            if (cuboDestino == totalImagens){
                cuboDestino=0;
            } else {
                ++cuboDestino;
            }

            ImagemInterna imgBMP;
            if (!CarregarBMP(listaImagens[i], imgBMP)) {
                cout << "Aviso: Falha ao carregar " << listaImagens[i] << endl;
                continue;
            }

            unsigned char imagem504[504];
            ConverterPara504Bytes(imgBMP, imagem504);

            // Envia via serial para a placa
            if (EnviarImagemSerial(hCom, cuboDestino, imagem504)) {
                if (AtivarImagemSerial(hCom, cuboDestino)) {
                    cout << "Exibido com sucesso!" << endl;
                }
            }

            //libera o bloco alocado antes de carregar a próxima imagem
            LiberarImagem(imgBMP);

            Sleep(tempoEsperaMs);
        }
    }

    CloseHandle(hCom);
    return 0;
}
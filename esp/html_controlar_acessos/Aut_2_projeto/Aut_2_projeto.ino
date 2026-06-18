#include <index.h>
#include <Presencas.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESPmDNS.h>  
#include <PubSubClient.h>

// Configurações RFID
#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN  10  // Pino SDA
#define RST_PIN 9   // Pino Reset

MFRC522 mfrc522(SS_PIN, RST_PIN); //Inicialização do leitor


// Servidor HTTP
WebServer server(80);
Preferences preferences; //Permite guardar dados na memória do ESP32

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); //horário de portugal

const int MAX_UTILIZADORES = 50; //Número de utilizadores autorizados a entrar na sala

// Estrutura dos dados
struct Utilizador {
  String id;
  String nome;
  String contacto;
  bool presente; 
  String horaEntrada;
};

struct RegistoLog {
  String hora;
  String nome;
  String acao;
};

Utilizador listaUtilizadores[MAX_UTILIZADORES]; 
String estadoLedHtml = "<span class=\"led-circulo led-vermelho\"></span><span class=\"led-texto\">Porta Trancada</span>";
unsigned long momentoAberturaPorta = 0;
RegistoLog historicoLogs[10]; //Últimos 10 movimentos
String mensagemStatus = ""; 

// Configurações MQTT
const char* mqtt_broker = "192.168.X.X"; // SUBSTITUIR IP
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqttClient(espClient);


//---------------------------  Funções  --------------------------------------

// Regista a hora exata e a ação (Entrada/Saída/Negado)
void adicionarLog(String nome, String acao) {
  timeClient.update(); 
  String horaAtual = timeClient.getFormattedTime(); 

  for(int i = 9; i > 0; i--) { historicoLogs[i] = historicoLogs[i-1]; }
  historicoLogs[0] = {horaAtual, nome, acao};
}

// Grava a lista de utilizadores na memória não-volátil
void guardarDadosNaMemoria() {
  preferences.begin("controlo_ac", false); 
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    preferences.putString(("id_" + String(i)).c_str(), listaUtilizadores[i].id);
    preferences.putString(("nm_" + String(i)).c_str(), listaUtilizadores[i].nome);
    preferences.putString(("ct_" + String(i)).c_str(), listaUtilizadores[i].contacto);
  }
  preferences.end(); 
}

// Carrega os dados guardados; Cria utilizador teste na 1vez
void carregarDadosDaMemoria() {
  preferences.begin("controlo_ac", true); 
  bool memoriaVazia = true;
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    listaUtilizadores[i].id = preferences.getString(("id_" + String(i)).c_str(), "");
    listaUtilizadores[i].nome = preferences.getString(("nm_" + String(i)).c_str(), "");
    listaUtilizadores[i].contacto = preferences.getString(("ct_" + String(i)).c_str(), "");
    listaUtilizadores[i].presente = false; 
    if(listaUtilizadores[i].id != "") memoriaVazia = false; 
  }
  preferences.end();
  if (memoriaVazia) {
    listaUtilizadores[0] = {"120203", "Iara Reis", "911223344", false, "--:--:--"};
    guardarDadosNaMemoria(); 
  }
}

// Lógica da entrada/saída com publicações MQTT
void registarPassagemFisica(String idLido) {
  bool encontrado = false;
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    if(listaUtilizadores[i].id == idLido) {
      encontrado = true;
      listaUtilizadores[i].presente = !listaUtilizadores[i].presente; 

      String tipoMovimento = "";
      String estadoMQTT = ""; // Variável para a mensagem MQTT
      
      //Quando uma pessoa entra
      if(listaUtilizadores[i].presente == true) {
        timeClient.update();
        listaUtilizadores[i].horaEntrada = timeClient.getFormattedTime();
        tipoMovimento = "Entrada";
        estadoMQTT = "entrou";
      } 
      //Quando uma pessoa sai
      else {
        tipoMovimento = "Saída";
        estadoMQTT = "saiu";
      }
      // PUBLICAÇÃO MQTT: Mensagem de Sucesso (Entrada/Saída)
      if(mqttClient.connected()) {
        String payload = "{\"id\":\"" + idLido + "\", \"nome\":\"" + listaUtilizadores[i].nome + "\", \"estado\":\"" + estadoMQTT + "\"}";
        mqttClient.publish("sala/acessos", payload.c_str());
      }
      adicionarLog(listaUtilizadores[i].nome, tipoMovimento);
      break;
    }
  }
  // Quando a entrada é recusada
  if (!encontrado) {
    adicionarLog("ID: " + idLido, "Acesso Negado");
    
    // PUBLICAÇÃO MQTT: Mensagem de Rejeição
    if(mqttClient.connected()) {
      String payload = "{\"id\":\"" + idLido + "\", \"nome\":\"Desconhecido\", \"estado\":\"recusado\"}";
      mqttClient.publish("sala/acessos", payload.c_str());
    }
  }
}

//------------------  FUNÇÕES DA INTERFACE WEB (HMI)  -----------------------------

//Pág. Principal
void enviarPaginaHTML() {
  String htmlDinamico = String(INDEX_HTML);
  
  String msgHtml = "";
  if (mensagemStatus != "") {
    if (mensagemStatus.indexOf("Erro") >= 0) msgHtml = "<div class='msg msg-error'>" + mensagemStatus + "</div>";
    else msgHtml = "<div class='msg msg-success'>" + mensagemStatus + "</div>";
    mensagemStatus = ""; 
  }
  htmlDinamico.replace("%MENSAGEM%", msgHtml);

  //Desenhar tabela
  int contador = 0;
  String tabela = "<table><tr><th>Identificador</th><th>Nome</th><th>Contacto</th></tr>";
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    if(listaUtilizadores[i].id != "") {
      contador++;
      String contactoExibir = listaUtilizadores[i].contacto;
      if(contactoExibir == "") contactoExibir = "<em>Não registado</em>";
      
      tabela += "<tr><td>" + listaUtilizadores[i].id + "</td><td>" + listaUtilizadores[i].nome + "</td><td>" + contactoExibir + "</td></tr>";
    }
  }
  tabela += "</table>";

  //Substituição dos marcadores pelas variáveis
  htmlDinamico.replace("%TABELA_IDS%", tabela);
  htmlDinamico.replace("%CONTADOR%", String(contador));
  htmlDinamico.replace("%MAXIMO%", String(MAX_UTILIZADORES));
  htmlDinamico.replace("%LED_STATUS%", estadoLedHtml);

  server.send(200, "text/html", htmlDinamico);

}

//Pág. das pessoas dentro da sala em tempo real
void enviarPaginaPresencas() {
  String htmlDinamico = String(PRESENCAS_HTML);
  int dentroDaSala = 0;
  String tabela = "<table><tr><th>ID</th><th>Nome do Utilizador</th><th>Hora de Entrada</th></tr>";
  
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    // Filtro para só aparecer na tabela os utilizadores presentes na sala 
    if(listaUtilizadores[i].id != "" && listaUtilizadores[i].presente == true) {
      dentroDaSala++;
      
      tabela += "<tr><td>" + listaUtilizadores[i].id + "</td><td>" + listaUtilizadores[i].nome + "</td><td><span class='status-in'>" + listaUtilizadores[i].horaEntrada + "</span></td></tr>";
    }
  }
  tabela += "</table>";
  
  //Substituição dos marcadores pelas variáveis
  htmlDinamico.replace("%TABELA_PRESENCAS%", tabela);
  htmlDinamico.replace("%TOTAL_DENTRO%", String(dentroDaSala));
  
  server.send(200, "text/html", htmlDinamico);
}


//Adicionar novo utilizador autorizado
void tratarAdicionarID() {
  if (server.hasArg("id_form") && server.hasArg("nome_form")) {
    String novoID = server.arg("id_form"); 
    String novoNome = server.arg("nome_form");
    String novoContacto = server.hasArg("contacto_form") ? server.arg("contacto_form") : "";
    novoID.trim(); novoNome.trim(); novoContacto.trim();
    
    bool jaExiste = false;
    for(int i=0; i<MAX_UTILIZADORES; i++) {
      if(listaUtilizadores[i].id == novoID) { jaExiste = true; break; }
    }
    
    if (jaExiste) {
      mensagemStatus = "Erro: O ID " + novoID + " já está autorizado!";
    } else {
      bool adicionado = false;
      for(int i=0; i<MAX_UTILIZADORES; i++) {
        if(listaUtilizadores[i].id == "") {
          listaUtilizadores[i] = {novoID, novoNome, novoContacto, false, "--:--:--"};
          mensagemStatus = "Sucesso: " + novoNome + " autorizado."; 
          guardarDadosNaMemoria(); 
          adicionado = true;
          break;
        }
      }
      if (!adicionado) mensagemStatus = "Erro: Lista cheia (" + String(MAX_UTILIZADORES) + ")! Remova um utilizador antes de adicionar outro.";
    }
  }
  server.sendHeader("Location", "/"); 
  server.send(303);
}


// Alterar contacto de um utilizador autorizado
void tratarAlterarContacto() {
  if (server.hasArg("id_alterar") && server.hasArg("contacto_alterar")) {
    String idAlterar = server.arg("id_alterar");
    String novoContacto = server.arg("contacto_alterar");
    idAlterar.trim(); novoContacto.trim();
    
    bool encontrado = false;
    for(int i = 0; i < MAX_UTILIZADORES; i++) {
      if(listaUtilizadores[i].id == idAlterar) {
        listaUtilizadores[i].contacto = novoContacto;
        encontrado = true;
        mensagemStatus = "Sucesso: Contacto de " + listaUtilizadores[i].nome + " atualizado.";
        guardarDadosNaMemoria(); 
        break;
      }
    }
    if (!encontrado) {
      mensagemStatus = "Erro: O ID " + idAlterar + " não foi encontrado no sistema.";
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}


//Remover autorização do utilizador
void tratarRemoverID() {
  if (server.hasArg("id_remover")) {
    String idParaRemover = server.arg("id_remover");
    idParaRemover.trim();
    
    bool encontrado = false;
    for(int i = 0; i < MAX_UTILIZADORES; i++) {
      if(listaUtilizadores[i].id == idParaRemover) {
        String nomeRemovido = listaUtilizadores[i].nome;
        listaUtilizadores[i].id = "";
        listaUtilizadores[i].nome = "";
        listaUtilizadores[i].contacto = "";
        listaUtilizadores[i].presente = false;
        listaUtilizadores[i].horaEntrada = "";
        encontrado = true;
        mensagemStatus = "Sucesso: O acesso para " + nomeRemovido + " foi removido.";
        guardarDadosNaMemoria(); 
        break;
      }
    }
    if (!encontrado) {
      mensagemStatus = "Erro: O ID " + idParaRemover + " não foi encontrado na lista.";
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

// Exportar dados em formato JSON 
void enviarLogsJSON() {
  server.sendHeader("Access-Control-Allow-Origin", "*"); 
  String json = "[";
  bool primeiro = true;
  for(int i = 0; i < 10; i++) {
    if(historicoLogs[i].hora != "") {
      if(!primeiro) json += ",";
      json += "{\"hora\":\"" + historicoLogs[i].hora + "\",\"nome\":\"" + historicoLogs[i].nome + "\",\"acao\":\"" + historicoLogs[i].acao + "\"}";
      primeiro = false;
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

// Se o MQTT cair tenta reconectar
void manterLigacaoMQTT() {
  if (!mqttClient.connected()) {
    Serial.print("A ligar ao Broker MQTT...");
    if (mqttClient.connect("ESP32_ControloEntradas")) {
      Serial.println(" Ligado ao MQTT!");
    } else {
      Serial.print(" Falhou. Erro: ");
      Serial.println(mqttClient.state());
    }
  }
  mqttClient.loop();
}
//-----------------------------  VOID SETUP()  ------------------------------------  
void setup() {

  carregarDadosDaMemoria(); 
  
  //Ligar à rede
  WiFi.begin("Iara's Galaxy A22 5G", "qudy3038"); //"E's Galaxy A12", "wesf4180"
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nServidor online no IP: " + WiFi.localIP().toString());

  
  mqttClient.setServer(mqtt_broker, mqtt_port);

  //DNS local
  if (MDNS.begin("acessos")) {
    Serial.println("DNS Ativo! Podes aceder via: http://acessos.local");
  } else {
    Serial.println("Erro ao configurar o DNS!");
  }

  timeClient.begin(); 

  //Botões da HMI 
  server.on("/", enviarPaginaHTML);
  server.on("/presencas", HTTP_GET, enviarPaginaPresencas); 
  server.on("/adicionar", HTTP_POST, tratarAdicionarID);
  server.on("/alterar", HTTP_POST, tratarAlterarContacto);
  server.on("/remover", HTTP_POST, tratarRemoverID); 
  server.on("/api/logs", HTTP_GET, enviarLogsJSON);
  
  server.on("/statusLed", []() {
    server.send(200, "text/html", estadoLedHtml);
  });

  server.begin();

  Serial.begin(115200);
  delay(2000);
  SPI.begin(); //Inicializa barramento SPI
  mfrc522.PCD_Init(); //Inicializa módulo MFRC522
  delay(4); 
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println("Aproxime o seu cartao/tag do leitor...");
}

//---------------------------------  VOID LOOP()  --------------------------------

void loop() {
  // Mantém as páginas HTML ativas
  server.handleClient(); 

  // Manter a ligação MQTT ativa
  manterLigacaoMQTT();

  if (momentoAberturaPorta > 0 && (millis() - momentoAberturaPorta > 9000)) {
    // Volta a trancar a porta
      estadoLedHtml = "<span class=\"led-circulo led-vermelho\"></span><span class=\"led-texto\">Porta Trancada</span>";
      momentoAberturaPorta = 0;
  }

  // Procura novos cartões 
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  // Tenta ler o número de série do cartão encontrado
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }
  // Extrai o UID do hardware e converte os bytes para uma String Hexadecimal
  String ID_Lido = "";
  for (byte i = 0; i < 4; i++) {
    String hex = String(mfrc522.uid.uidByte[i], HEX);
    if (hex.length() == 1) hex = "0" + hex; // Garante dois dígitos por byte
    ID_Lido += hex;
  }

  ID_Lido.toUpperCase(); // Converte letras em maiúsculas

  // Comando para o leitor ignorar cartões parados no sensor (evita leituras repetidas)
  mfrc522.PICC_HaltA();

  Serial.println("\n--- Cartão detetado: " + ID_Lido + " ---");
      
  //Validação Local: Verifica se o ID lido está autorizado
  bool temAcesso = false;
  String nomeDaPessoa = "Desconhecido";
      
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    if(listaUtilizadores[i].id == ID_Lido && listaUtilizadores[i].id != "") {
      temAcesso = true;
      nomeDaPessoa = listaUtilizadores[i].nome; 
      break;
    }
  }

  //Registar dados
  registarPassagemFisica(ID_Lido);

  //Simulação da porta
  if (temAcesso) {
    Serial.println("-> Acesso PERMITIDO para: " + nomeDaPessoa);
    Serial.println(">>> PORTA A ABRIR <<< (Trinco elétrico ativado)");
    estadoLedHtml = "<span class=\"led-circulo led-verde\"></span><span class=\"led-texto\">Porta Aberta</span>";
    momentoAberturaPorta = millis(); // Liga o cronómetro de 3 segundos
    

  } else {
    Serial.println("-> Acesso RECUSADO.");
    Serial.println("!!! PORTA TRANCADA !!! (Alarme visual ativado)");
    estadoLedHtml = "<span class=\"led-circulo led-vermelho\"></span><span class=\"led-texto\">Acesso Recusado</span>";
    momentoAberturaPorta = millis(); // Liga o cronómetro de 3 segundos
  
  }
}

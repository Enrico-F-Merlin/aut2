#include <index.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESPmDNS.h> 

WebServer server(80);
Preferences preferences;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600, 60000); 

const int MAX_UTILIZADORES = 50; 

struct Utilizador {
  String id;
  String nome;
  String contacto;
  bool presente; 
};

struct RegistoLog {
  String hora;
  String nome;
  String acao;
};

Utilizador listaUtilizadores[MAX_UTILIZADORES]; 
RegistoLog historicoLogs[10]; 
String mensagemStatus = ""; 

void adicionarLog(String nome, String acao) {
  timeClient.update(); 
  String horaAtual = timeClient.getFormattedTime(); 
  for(int i = 9; i > 0; i--) { historicoLogs[i] = historicoLogs[i-1]; }
  historicoLogs[0] = {horaAtual, nome, acao};
}

void guardarDadosNaMemoria() {
  preferences.begin("controlo_ac", false); 
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    preferences.putString(("id_" + String(i)).c_str(), listaUtilizadores[i].id);
    preferences.putString(("nm_" + String(i)).c_str(), listaUtilizadores[i].nome);
    preferences.putString(("ct_" + String(i)).c_str(), listaUtilizadores[i].contacto);
  }
  preferences.end(); 
}

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
    listaUtilizadores[0] = {"120203", "Carlos Antunes", "911223344", false};
    guardarDadosNaMemoria(); 
  }
}

void registarPassagemFisica(String idLido) {
  bool encontrado = false;
  for(int i = 0; i < MAX_UTILIZADORES; i++) {
    if(listaUtilizadores[i].id == idLido) {
      encontrado = true;
      listaUtilizadores[i].presente = !listaUtilizadores[i].presente; 
      String tipoMovimento = listaUtilizadores[i].presente ? "Entrada" : "Saída";
      adicionarLog(listaUtilizadores[i].nome, tipoMovimento);
      break;
    }
  }
  if (!encontrado) {
    adicionarLog("ID: " + idLido, "Acesso Negado");
  }
}

void enviarPaginaHTML() {
  String htmlDinamico = String(INDEX_HTML);
  
  String msgHtml = "";
  if (mensagemStatus != "") {
    if (mensagemStatus.indexOf("Erro") >= 0) msgHtml = "<div class='msg msg-error'>" + mensagemStatus + "</div>";
    else msgHtml = "<div class='msg msg-success'>" + mensagemStatus + "</div>";
    mensagemStatus = ""; 
  }
  htmlDinamico.replace("%MENSAGEM%", msgHtml);
  
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
  
  htmlDinamico.replace("%TABELA_IDS%", tabela);
  htmlDinamico.replace("%CONTADOR%", String(contador));
  htmlDinamico.replace("%MAXIMO%", String(MAX_UTILIZADORES));
  
  server.send(200, "text/html", htmlDinamico);
}

void tratarAdicionarID() {
  if (server.hasArg("id_form") && server.hasArg("nome_form")) {
    String novoID = server.arg("id_form"); String novoNome = server.arg("nome_form");
    String novoContacto = server.hasArg("contacto_form") ? server.arg("contacto_form") : "";
    novoID.trim(); novoNome.trim(); novoContacto.trim();
    
    bool jaExiste = false;
    for(int i=0; i<MAX_UTILIZADORES; i++) {
      if(listaUtilizadores[i].id == novoID) { jaExiste = true; break; }
    }
    
    if (jaExiste) {
      mensagemStatus = "Erro: O ID " + novoID + " já se encontra autorizado!";
    } else {
      bool adicionado = false;
      for(int i=0; i<MAX_UTILIZADORES; i++) {
        if(listaUtilizadores[i].id == "") {
          listaUtilizadores[i] = {novoID, novoNome, novoContacto, false};
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

void setup() {
  Serial.begin(9600);
  carregarDadosDaMemoria(); 
  
  WiFi.begin("Iara's Galaxy A22 5G", "qudy3038"); 
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nServidor online no IP: " + WiFi.localIP().toString());

  
  if (MDNS.begin("acessos")) {
    Serial.println("DNS Ativo! Podes aceder via: http://acessos.local");
  } else {
    Serial.println("Erro ao configurar o DNS!");
  }

  timeClient.begin(); 

  server.on("/", enviarPaginaHTML);
  server.on("/adicionar", HTTP_POST, tratarAdicionarID);
  server.on("/alterar", HTTP_POST, tratarAlterarContacto);
  server.on("/remover", HTTP_POST, tratarRemoverID); 
  server.on("/api/logs", HTTP_GET, enviarLogsJSON);
  
  server.begin();
}

void loop() {
  server.handleClient();
}
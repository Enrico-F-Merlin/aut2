const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Gestão de Acessos</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 30px; background-color: #ffffff; color: #000000; }
        h2 { color: #004085; border-bottom: 2px solid #004085; padding-bottom: 8px; margin-bottom: 20px; }
        h3 { color: #333333; margin-top: 25px; margin-bottom: 12px; }
        
        /* Alterado para cooperar com o alinhamento do LED */
        table { border-collapse: collapse; width: 100%; margin-bottom: 0; }
        th, td { border: 1px solid #444444; text-align: left; padding: 8px; font-size: 14px; }
        th { background-color: #e9ecef; color: #212529; font-weight: bold; }
        tr:nth-child(even) { background-color: #f8f9fa; }
        
        .form-group { margin-bottom: 12px; }
        label { display: inline-block; width: 110px; font-size: 14px; font-weight: bold; color: #444444; }
        input[type=text] { padding: 6px; font-size: 14px; width: 250px; border: 1px solid #777777; }
        .btn { padding: 8px 16px; font-size: 14px; border: 1px solid #333333; cursor: pointer; font-weight: bold; margin-top: 5px; }
        
        .btn-add { background-color: #28a745; color: white; border-color: #1e7e34; }
        .btn-remove { background-color: #dc3545; color: white; border-color: #bd2130; }
        .btn-update { background-color: #17a2b8; color: white; border-color: #117a8b; }
        .btn-nav { background-color: #6c757d; color: white; border-color: #5a6268; text-decoration: none; display: inline-block; }
        
        .msg { padding: 10px; margin-bottom: 20px; width: 69%; min-width: 496px; font-weight: bold; border: 2px solid; }
        .msg-error { background-color: #f8d7da; color: #721c24; border-color: #f5c6cb; }
        .msg-success { background-color: #d4edda; color: #155724; border-color: #c3e6cb; }
        
        .seccao-form { margin-bottom: 25px; padding: 15px; border: 1px dashed #ccc; width: 67%; min-width: 480px; }
        .contador { font-weight: bold; color: #004085; margin-bottom: 15px; }

        /* --- NOVOS ESTILOS PARA O LED VIRTUAL --- */
        .tabela-bloco { display: flex; align-items: flex-start; gap: 25px; width: 70%; min-width: 500px; margin-bottom: 25px; }
        .tabela-wrapper { flex: 1; }
        .led-card { padding: 12px 18px; border: 1px solid #ccc; background-color: #f8f9fa; border-radius: 4px; display: flex; align-items: center; gap: 12px; white-space: nowrap; }
        .led-circulo { width: 16px; height: 16px; border-radius: 50%; display: inline-block; }
        .led-verde { background-color: #28a745; }
        .led-vermelho { background-color: #dc3545; }
        .led-texto { font-size: 14px; font-weight: bold; color: #333333; }
    </style>
</head>
<body>

    <h2>Sistema de Monitorização - Controlo de Acesso</h2>

    <a href="/presencas" class="btn" style="background-color: #004085; color: white; text-decoration: none; margin-bottom: 20px; display: inline-block;">Ver Quem Está na Sala</a>
    
    <a href="http://rpi-756.local:8080" class="btn btn-add" style="margin-bottom: 20px; text-decoration: none; display: inline-block;">Ver Mapa Dinâmico da Sala</a>
    
    %MENSAGEM%

    <div class="contador">Utilizadores registados: %CONTADOR% / %MAXIMO%</div>
    
    <div class="tabela-bloco">
        <div class="tabela-wrapper">
            %TABELA_IDS%
        </div>
        <div id="bloco-led" class="led-card">
            %LED_STATUS%
        </div>
    </div>

    <div class="seccao-form">
        <h3>Autorizar Novo Utilizador</h3>
        <form action="/adicionar" method="POST">
            <div class="form-group">
                <label>Identificador: </label>
                <input type="text" name="id_form" required maxlength="8" minlength="1">
            </div>
            <div class="form-group">
                <label>Nome: </label>
                <input type="text" name="nome_form" required maxlength="30">
            </div>
            <div class="form-group">
                <label>Contacto: </label>
                <input type="text" name="contacto_form" maxlength="9" pattern="[0-9]{9}">
            </div>
            <input type="submit" class="btn btn-add" value="Autorizar Utilizador">
        </form>
    </div>

    <div class="seccao-form">
        <h3>Alterar Contacto</h3>
        <form action="/alterar" method="POST">
            <div class="form-group">
                <label>Identificador: </label>
                <input type="text" name="id_alterar" required maxlength="8" minlength="1">
            </div>
            <div class="form-group">
                <label>Novo Contacto: </label>
                <input type="text" name="contacto_alterar" required maxlength="9" pattern="[0-9]{9}">
            </div>
            <input type="submit" class="btn btn-update" value="Atualizar Contacto">
        </form>
    </div>

    <div class="seccao-form">
        <h3>Remover Acesso</h3>
        <form action="/remover" method="POST">
            <div class="form-group">
                <label>Identificador: </label>
                <input type="text" name="id_remover" required maxlength="8" minlength="1">
            </div>
            <input type="submit" class="btn btn-remove" value="Remover Acesso">
        </form>
    </div>
    <script>
            setInterval(function() {
                fetch('/statusLed')
                 .then(response => response.text())
                 .then(dadosHtml => {
                     document.getElementById('bloco-led').innerHTML = dadosHtml;
                  })
                  .catch(err => console.log("Erro ao ler estado do LED: ", err));
            }, 1000); // Executa a cada 1 segundo (em background, sem piscar a página)
    </script>



</body>
</html>
)rawliteral";
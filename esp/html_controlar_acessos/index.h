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
        
        table { border-collapse: collapse; width: 70%; min-width: 500px; margin-bottom: 25px; }
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
    </style>
</head>
<body>

    <h2>Sistema de Monitorização - Controlo de Acesso</h2>
    
    <a href="http://lidar.local" class="btn btn-nav" style="margin-bottom: 20px;">Ver Mapa dinâmico da sala</a>
    
    %MENSAGEM%

    <div class="contador">Utilizadores registados: %CONTADOR% / %MAXIMO%</div>
    
    %TABELA_IDS%

    <div class="seccao-form">
        <h3>Autorizar Novo Utilizador</h3>
        <form action="/adicionar" method="POST">
            <div class="form-group">
                <label>Identificador: </label>
                <input type="text" name="id_form" required maxlength="6" minlength="6" pattern="[0-9]{6}">
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
                <input type="text" name="id_alterar" required maxlength="6" minlength="6" pattern="[0-9]{6}">
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
                <input type="text" name="id_remover" required maxlength="6" minlength="6" pattern="[0-9]{6}">
            </div>
            <input type="submit" class="btn btn-remove" value="Remover Acesso">
        </form>
    </div>

</body>
</html>
)rawliteral";
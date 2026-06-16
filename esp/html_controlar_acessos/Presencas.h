const char PRESENCAS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta http-equiv="refresh" content="2"><title>Controlo de Acessos - Monitorização</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 30px; background-color: #ffffff; color: #000000; }
        h2 { color: #004085; border-bottom: 2px solid #004085; padding-bottom: 8px; margin-bottom: 20px; }
        
        table { border-collapse: collapse; width: 60%; min-width: 450px; margin-bottom: 25px; }
        th, td { border: 1px solid #444444; text-align: left; padding: 8px; font-size: 14px; }
        th { background-color: #e9ecef; color: #212529; font-weight: bold; }
        tr:nth-child(even) { background-color: #f8f9fa; }
        
        .btn { padding: 6px 14px; font-size: 14px; border: 1px solid #333333; cursor: pointer; font-weight: bold; text-decoration: none; display: inline-block; margin-right: 10px; }
        .btn-nav { background-color: #6c757d; color: white; border-color: #545b62; }
        .btn-lidar { background-color: #004085; color: white; border-color: #002752; }
        
        .status-in { color: #28a745; font-weight: bold; }
        .status-out { color: #6c757d; font-weight: bold; }
        .contador-painel { font-size: 16px; font-weight: bold; margin-bottom: 15px; color: #333; }
    </style>
</head>
<body>

    <h2>Monitorização de Presença em Tempo Real</h2>
    
    <div style="margin-bottom: 20px;">
        <a href="/" class="btn btn-nav">Voltar à Gestão de Utilizadores</a>
        
        <a href="http://rpi-756.local:8080" class="btn btn-lidar">Ver Mapa Dinâmico da Sala</a>
    </div>

    <div class="contador-painel">
        Pessoas dentro da sala: <span style="color: #004085;">%TOTAL_DENTRO%</span>
    </div>
    
    %TABELA_PRESENCAS%

</body>
</html>
)rawliteral";
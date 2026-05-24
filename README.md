# Automação 2 - Segurança no chão de fábrica

## Introdução
A sensorização é uma parte muito importante do ambiente fabril, não só nas linhas de produção, como componente nuclear do processo de automação, mas também no estabelecimento e manutenção de um ambiente seguro. Dessa maneira, é de suma importância que o sistema de segurança estabelecido seja fiável, eficiente e de tão fácil uso quanto possível.

**Adicionar imagem de um esquema do amiente a ser monitorado**

O sistema desenvolvido consiste em um conjunto de sensores, ligados por um barramento, de forma que existam restrições de entrada e limitação do espaço de circulação. Além disso, também podem haver sensores que observam as condições de equipamentos.

## Descrição do Sistema

O sistema foi feito de forma a ser de uso fáci, intuitivo e de conveniente manutenção.

### Hardware

A parte física do sistema é formada por um conjunto de sensores

**Adicionar imagem dum LiDAR**

**LiDAR** - O LiDAR foi utilizado de forma a detetar pessoas no interior do ambiente a ser observado, e avisar sobre seu posicionamento. Esse aviso vai ser relativo a zonas de três tipos - **seguro**, **aviso** e **perigo**.

**Adicionar imagem do sensor ultrassom**

**Sensor Ultrassónico** - O sensor ultrassónico foi utilizado para reprensentar um sensor genérico que observa um equipamento. Neste caso, utilizamos esse sensor para imitar um observador de nivél em um tanque de água.

**Adicionar imagem do leitor RFID**

**Leitor RFID** - O leitor RFID foi posto à entrada do ambiente a ser controlado de forma a limitar o acesso apenas para pessoas autorizadas.

**Adicionar imagem do barramento CAN**

**CAN 2.0A** - O CAN é um protocolo, de nível físico e digital, de comunicação em barramento. Ele vai permitir a adição e remoção de sensores de forma modular, assim evitando que defeitos ou manutenção em um módulo não interrompa funcionamento dos outros. 

### Software

Para mostrar a possibilidade de um sistema descentralizado, utilizamos uma Raspberry Pi para fazer o processamento dos dados e apresenta-los em uma página web, de uma rede local, enquanto que a gestão de permissões de acesso foi desenvolvida em uma ESP32 aparte. Dessa forma, romovemos a comunicação wireless por WiFi entre diferentes servidores onde a falha de um não implica parar o outro.

**Adicionar imagem da página de sensores**

Nesta página podemos observar um ***live feed*** das leituras do LiDAR, assim como uma ***dashboard*** da interpretação dos dados. Além disso, o valor das leituras do sensor ultrassónico são apresentados de forma dinâmica com a ilustração de um tanque da água. Por fim, podemos também observar a listagem das pessoas que estão no interior do ambiente controlado a partir das leituras do RFID.

**Adicionar imagem da página de gestão de permissões**

*descrever página e funcionamento*

### Tecnologias(?)




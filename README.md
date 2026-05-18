# Branddetekteringssystem. (Del 1)

# Formål
Systemet overvåger et miljø for brand- og sikkerhedsrisici ved hjælp af fem sensorer og en TinyML-model trænet i Edge Impulse.
Hardware-komponenter
Systemet bruger en SODAQ Explorer som hovedcontroller. 
Der er tilsluttet en BMP280 temperatursensor via I2C, 
en intern MCP9700 temperatursensor på analog pin A6 som backup, 
en MQ-9 gas- og røgsensor på analog pin A5, en KY-026 flamme-sensor på analog pin A2, 
og en PIR-bevægelsessensor på digital pin D7. 
Derudover bruges en RGB LED til statusindikering og et LoRa-modul på Serial2 til trådløs dataoverførsel til The Things Network. 

# TinyML klassifikation
Modellen er trænet til at genkende fem forskellige tilstande. Normal betyder normal drift og udløser ingen alarm. 
Motion betyder bevægelse – en person er til stede i rummet – 
og dette giver kun en information, ikke en alarm. Gas betyder gaslækage eller lightergas og udløser en alarm. 
Smoke betyder røgudvikling og udløser en alarm. Heat betyder unormal varmestigning, for eksempel fra en varmepistol, og udløser også en alarm.

# Datastrøm
Sensordata går fra sensorerne til feature extraction, hvor der laves fem features. 
Derefter kører TinyML inferens, og resultatet sendes via LoRaWAN til The Things Network, 
der videresender data til InfluxDB og MariaDB, hvorefter det kan vises i Grafana.
## Data flow
SODAQ Explore → LoRaWAN → The Things Network → MQTT → Python Backend
                                                        ↓
                                          InfluxDB / MariaDB / Telegram


# LoRaWAN Payload
Data sendes i et kompakt binært format på 13 til 14 bytes for at spare båndbredde. 
Byte 0 og 1 indeholder BMP280 temperaturen som en 16-bit integer med 0,01 grads opløsning. 
Byte 2 og 3 indeholder MCP9700 temperaturen i samme format. 
Byte 4 og 5 indeholder MQ-9 gasværdien som en 16-bit unsigned integer. 
Byte 6 og 7 indeholder flammesensorens analoge værdi. Byte 8 indeholder PIR-status som 0 eller 1. 
Byte 9 indeholder label index, som er et tal fra 0 til 4 der angiver hvilken klasse modellen har forudsagt. 
Byte 10 og 11 indeholder konfidensen som en 16-bit integer fra 0 til 10000, hvor 10000 svarer til 100 procent. 
Byte 12 indeholder flags hvor bit 0 er alarm og bit 1 er motion. 

# Demo-mode
Du kan sætte DEMO_MODE til true i koden for at køre med simulerede scenarier, hvilket er nyttigt til test uden fysiske sensorer. 
Sekvensen kører: Normal i 20 sekunder, derefter Motion i 10 sekunder, derefter Normal i 20 sekunder, 
derefter Gas i 10 sekunder, derefter Normal i 20 sekunder, 
derefter Smoke i 10 sekunder, og så gentages sekvensen.


# TTN MQTT Listener til SODAQ Explore Branddetektionssystem

Python-scriptet fungerer som backend-modtager for branddetektionssystemet udviklet med SODAQ Explore, TinyML og LoRaWAN. 
Scriptet modtager sensordata via MQTT fra The Things Network (TTN), dekoder binære payloads fra SODAQ Explore-enheden 
og gemmer data i både InfluxDB og MariaDB. Systemet sender samtidig notifikationer via Telegram ved modtagne målinger og alarmsituationer.

## Formål

Formålet med systemet er at overvåge brandrelaterede forhold såsom temperatur, gas, røg, flammer og bevægelse ved hjælp af sensorer og TinyML. SODAQ Explore-enheden indsamler sensordata, udfører lokal klassifikation med Edge Impulse TinyML og sender resultaterne via LoRaWAN til TTN. Python-scriptet fungerer herefter som central datamodtager og behandlingsserver.

## MQTT-kommunikation

Systemet anvender MQTT-kommunikation til at modtage data fra The Things Network. Scriptet fungerer som en MQTT-klient ved hjælp af biblioteket `paho-mqtt`.

Der oprettes en sikker TLS-forbindelse til TTN’s MQTT-broker på port 8883. Scriptet abonnerer på uplink-topic’et:

v3/<application-id>/devices/+/up 


# Python script
Python-scriptet modtager herefter beskeden automatisk gennem callback-funktionen on_message().
MQTT anvendes i systemet, fordi protokollen er letvægtsbaseret, effektiv til IoT-kommunikation 
og velegnet til realtidsdata mellem enheder og servere.

Scriptet indeholder følgende funktioner:
Opretter sikker MQTT-forbindelse til TTN via TLS.
Modtager uplink-data fra LoRaWAN-enheder.
Dekoder binær payload fra SODAQ Explore.
Udtrækker sensorværdier fra BMP280, MCP9700, MQ-9, flame sensor og PIR.
Udtrækker TinyML-resultater såsom label, confidence og alarmstatus.
Gemmer målinger i InfluxDB som tidsseriedata.
Gemmer alarmhændelser i MariaDB.
Gemmer statistik over målinger og alarmer i MariaDB.
Sender Telegram-notifikationer ved modtagne målinger og alarmer.
Logger RSSI og SNR fra LoRaWAN-signalet.

# InfluxDB
InfluxDB anvendes til lagring af sensordata som tidsseriedata. Scriptet opretter 
datapunkter med temperaturmålinger, gasmålinger, flammeværdier, PIR-status, confidence samt alarmstatus.

# MariaDB
MariaDB anvendes til relationel lagring af:
Alarmhændelser, Statistik over målinger og alarmer.
Når systemet registrerer en alarm, gemmes hændelsen automatisk i databasen 
sammen med relevante sensorværdier og klassifikationsresultater. 

# Telegram
Scriptet sender Telegram-beskeder ved modtagne målinger. Hvis TinyML-modellen registrerer en alarmtilstand såsom røg,
varme eller gas, markeres beskeden tydeligt som alarm.
Telegram-beskeden indeholder blandt andet:

Device ID
Temperatur
Gasværdi
Flame sensorværdi
PIR-status
TinyML-label
Confidence
RSSI og SNR
Tidspunkt

Fejlhåndtering

Scriptet håndterer blandt andet:

MQTT-forbindelsesfejl
Payload-dekodningsfejl
Databasefejl
Telegram-fejl
LoRaWAN-datafejl

Alle fejl logges i terminalen for fejlsøgning og overvågning.

## Brute-force sikkerhedsanalyse

Dette repository indeholder også et Python-script til analyse af brugernavn- og passwordstyrke. 
Scriptet beregner, hvor svært det teoretisk ville være for en angriber at gætte en kombination af 
brugernavn og password ved hjælp af et brute-force angreb.
Scriptet fungerer som et terminalbaseret analyseværktøj. 
Brugeren indtaster et brugernavn og et password, 
hvorefter programmet undersøger længde, tegnsæt, antal mulige kombinationer, estimeret knækningstid og samlet sikkerhedsstyrke.
Passwordet indtastes skjult i terminalen ved hjælp af `getpass`, så det ikke vises på skærmen under indtastning.
Programmet gemmer ikke data og sender heller ikke oplysninger videre til eksterne tjenester.

### Formål
Formålet med scriptet er at demonstrere, hvordan passwordstyrke kan vurderes matematisk ud 
fra længde og tegnvariation. Scriptet kan bruges som et læringsværktøj inden for IT-sikkerhed 
til at vise, hvorfor lange og komplekse passwords er sværere at brute-force.
Det viser samtidig forskellen mellem svage og stærke adgangskoder 
ved at beregne, hvor mange kombinationer en angriber teoretisk skal afprøve.

### Hvordan scriptet virker
Scriptet starter med en velkomstskærm, hvor brugeren får en kort forklaring af, 
hvad brute-force analyse betyder. Herefter indtaster brugeren et brugernavn og et password.
Programmet analyserer derefter, hvilke typer tegn der indgår i både brugernavn og password. 
Det undersøger blandt andet, om teksten indeholder små bogstaver, store bogstaver, tal og symboler. 
Hver tegntype øger det samlede tegnrum, hvilket betyder, at der bliver flere mulige kombinationer.
Hvis et password eksempelvis kun bruger små bogstaver, er tegnrummet 26 tegn. 
Hvis det både bruger små bogstaver, store bogstaver, tal og symboler, bliver tegnrummet væsentligt større. 
Et større tegnrum kombineret med længere længde giver flere mulige kombinationer og dermed højere sikkerhed.

### Beregning af kombinationer
Scriptet beregner først antallet af mulige brugernavne og mulige passwords ud fra følgende princip:

# Angrebshastighed
Brugeren vælger selv en angrebshastighed ud fra tre niveauer:
1. Simpel PC
2. Gaming PC
3. Professionel rig

Disse niveauer repræsenterer forskellige mængder forsøg per sekund. 
På baggrund af den valgte hastighed beregner programmet, hvor lang tid det cirka ville tage at gennemgå alle kombinationer.
Entropi bruges som et mål for, hvor svært kombinationen er at gætte.
Jo højere bit-værdi, desto stærkere vurderes brugernavn- og passwordkombinationen.

Programmet kategoriserer resultatet som:
Meget svag
Svag
Middel
Stærk
Meget stærk.

Derudover vises en visuel styrkebar i terminalen, så brugeren hurtigt kan se den overordnede sikkerhedsvurdering.

# Anbefalinger
Efter analysen giver programmet konkrete anbefalinger. Hvis passwordet er for kort eller har lav entropi, 
anbefaler scriptet at vælge et længere og stærkere password.
Generelt anbefales det at bruge mindst 12 tegn og kombinere små bogstaver, store bogstaver, tal og symboler.


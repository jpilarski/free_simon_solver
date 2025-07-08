from flask import Flask, render_template, jsonify
import serial
from threading import Thread, Lock
import time
import sys
import logging
import sqlite3
from datetime import datetime

#Wyłączenie logowania Flask
log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

app = Flask(__name__)

#Inicjalizacja połączenia z bazą danych
conn = sqlite3.connect('game_data.db', check_same_thread=False)
cursor = conn.cursor()

#Tworzenie tabeli w bazie danych
cursor.execute('''CREATE TABLE IF NOT EXISTS game_log (
                    id INTEGER PRIMARY KEY,
                    timestamp TEXT,
                    type TEXT,
                    data TEXT
                 )''')
conn.commit()

@app.after_request
def add_cors_headers(response):
    #Dodanie nagłówków CORS do odpowiedzi
    response.headers['Access-Control-Allow-Origin'] = '*'
    response.headers['Access-Control-Allow-Headers'] = 'Content-Type'
    return response

#Globalny stan gry
game_state = {
    'sequence': '',         #Sekwencja oczekiwanych kolorów
    'user_step': 0,         #Aktualny krok użytkownika w sekwencji
    'history': [],          #Historia działań w grze
    'expected_color': None, #Oczekiwany kolor do wciśnięcia
    'game_over': None       #Informacja, czy gra się zakończyła
}
lock = Lock()               #Blokada do synchronizacji dostępu do game_state

#Mapowanie kolorów
COLOR_MAP = {
    'R': {'name': 'Red', 'svg': '1.svg'},    #Kolor czerwony
    'G': {'name': 'Green', 'svg': '2.svg'},  #Kolor zielony
    'B': {'name': 'Blue', 'svg': '3.svg'},   #Kolor niebieski
    'Y': {'name': 'Yellow', 'svg': '4.svg'}  #Kolor żółty
}

def save_to_db(event_type, data):
    #Funkcja zapisująca dane do bazy danych
    try:
        timestamp = datetime.now().isoformat() #Pobranie aktualnego czasu
        cursor.execute(
            "INSERT INTO game_log (timestamp, type, data) VALUES (?, ?, ?)",
            (timestamp, event_type, data)
        )
        conn.commit() #Zatwierdzenie zmian w bazie danych
    except Exception as e:
        print(f"Błąd zapisu do bazy danych: {e}")

def serial_reader():
    #Funkcja odczytująca dane z portu szeregowego
    ser = serial.Serial('/dev/ttyS0', 9600, timeout=1) #Inicjalizacja portu szeregowego

    while True:
        try:
            line = ser.readline().decode('utf-8').strip() #Odczyt linii z portu szeregowego

            if line:
                print(f"Odebrano z Arduino: *{line}*")
                
                #Zapis do bazy danych
                if line.startswith('seq:'):
                    save_to_db('sequence', line) #Zapis sekwencji do bazy danych
                    with lock:
                        seq = line.split(': ')[1] #Wyciągnięcie sekwencji z komunikatu
                        if not all(c in {'R','G','B','Y'} for c in seq):
                            #Sprawdzenie poprawności sekwencji
                            print(f"Nieprawidłowa sekwencja: {seq}")
                            game_state['history'].append(f'BŁĄD: Nieprawidłowa sekwencja {seq}')
                            continue
                            
                        #Zaktualizowanie globalnego stanu gry
                        game_state['sequence'] = seq
                        game_state['user_step'] = 0
                        game_state['history'] = [f'Nowa sekwencja: {seq}']
                        game_state['game_over'] = None

                elif line.startswith('usr:'):
                    save_to_db('user_input', line) #Zapis danych użytkownika do bazy danych
                    with lock:
                        if game_state.get('game_over'):
                            continue #Ignorowanie wejścia, jeśli gra się zakończyła
                            
                        pressed_color = line.split(': ')[1] #Wyciągnięcie koloru wciśniętego przez użytkownika
                        
                        if pressed_color == 'error':
                            #Obsługa błędu zgłoszonego przez użytkownika
                            game_state['history'].append('Przepraszamy! Wygląda na to, że coś poszło nie tak z naszej strony. Niestety, przegrałeś. Spróbuj ponownie!')
                            game_state['game_over'] = 'system_error'
                            save_to_db('system_error', game_state['sequence'])
                            continue

                        if pressed_color not in COLOR_MAP:
                            #Obsługa nieznanego koloru
                            game_state['history'].append(f'BŁĄD: Nieznany kolor {pressed_color}')
                            continue
                            
                        if not game_state['sequence']:
                            continue #Ignorowanie, jeśli nie ma aktywnej sekwencji
                            
                        expected_color = game_state['sequence'][game_state['user_step']] #Oczekiwany kolor
                        
                        if pressed_color != expected_color:
                            #Obsługa błędnego wciśnięcia przycisku
                            game_state['history'].append('BŁĄD: Nacisnąłeś zły przycisk! Koniec gry!')
                            game_state['game_over'] = 'lost'
                            save_to_db('error', game_state['sequence'])
                        else:
                            #Obsługa poprawnego wciśnięcia przycisku
                            game_state['history'].append(f'OK: Poprawny przycisk ({pressed_color})')
                            game_state['user_step'] += 1
                            
                            if game_state['user_step'] >= len(game_state['sequence']):
                                #Zakończenie gry w przypadku ukończenia sekwencji
                                game_state['history'].append('SUKCES: Ukończyłeś sekwencję!')
                                game_state['game_over'] = 'won'
                                save_to_db('success', game_state['sequence'])
                                
                            else:
                                #Ustawienie kolejnego oczekiwanego koloru
                                game_state['expected_color'] = game_state['sequence'][game_state['user_step']]

        except Exception as e:
            print(f'Błąd: {e}')
        time.sleep(0.1) #Opóźnienie odczytu

@app.route('/')
def index():
    #Funkcja zwracająca stronę główną
    return render_template('index.html', color_map=COLOR_MAP)

@app.errorhandler(500)
def handle_server_error(e):
    #Obsługa błędów serwera
    print(f"Server error: {e}")
    return jsonify(error=str(e)), 500

@app.route('/status')
def get_status():
    #Endpoint zwracający status gry
    with lock:
        try:
            current_color = None
            game_status = game_state.get('game_over')
            
            #Walidacja sekwencji
            valid_sequence = ''.join([c for c in game_state.get('sequence', '') if c in COLOR_MAP])
            if valid_sequence != game_state.get('sequence', ''):
                game_state['sequence'] = valid_sequence
                game_state['user_step'] = min(game_state['user_step'], len(valid_sequence))

            if not game_state['sequence']:
                #Jeśli brak aktywnej sekwencji
                return jsonify({
                    'history': [],
                    'current_color': None,
                    'full_sequence': '',
                    'progress': 0,
                    'game_status': None
                })

            if not game_status and game_state['user_step'] < len(game_state['sequence']):
                current_color = game_state['sequence'][game_state['user_step']] #Ustal aktualny kolor
            
            response = jsonify({
                'history': game_state['history'][-5:],
                'current_color': current_color,
                'full_sequence': game_state['sequence'],
                'progress': game_state['user_step'],
                'game_status': game_status
            })
            response.headers['Content-Type'] = 'application/json; charset=utf-8'  #Dodanie nagłówka
            return response
        except Exception as e:
            print(f"Status error: {e}")
            return jsonify(error=str(e)), 500

#Nowy endpoint do pobierania historii z bazy danych
@app.route('/history')
def get_db_history():
    #Funkcja zwracająca historię z bazy danych
    try:
        #Zapytanie do bazy danych, filtrujące tylko sukcesy i błędy
        cursor.execute('''SELECT timestamp, type, data 
                        FROM game_log 
                        WHERE type IN ('success', 'error', 'system_error')
                        ORDER BY timestamp DESC 
                        LIMIT 100''')
        records = cursor.fetchall()
        response = jsonify([{
            'timestamp': record[0],
            'type': record[1],
            'data': record[2]
        } for record in records])
        response.headers['Content-Type'] = 'application/json; charset=utf-8'
        return response
    except Exception as e:
        return jsonify({'error': str(e)}), 500
    
if __name__ == '__main__':
    #Uruchomienie wątku do odbierania danych z Arduino
    thread = Thread(target=serial_reader)
    thread.daemon = True
    thread.start()
    
    #Uruchomienie serwera Flask
    app.run(host='0.0.0.0', port=1111, debug=False, threaded=True, use_reloader=False)

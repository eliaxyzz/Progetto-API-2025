import random
import time
import os

def generate_dish_names(n):
    bases = [
        "pizza", "pasta", "risotto", "sushi", "torta", "carne", "pesce", "pollo", 
        "insalata", "zuppa", "gelato", "vino", "birra", "arrosto", "lasagne", 
        "braciola", "salmone", "patatine", "formaggio", "pane", "gnocchi", 
        "ravioli", "bistecca", "spiedini", "tiramisu", "focaccia", "bruschetta", 
        "polenta", "arancini", "polpette", "tartare", "carpaccio", "frittata", 
        "minestrone", "vellutata", "couscous", "paella", "hamburger", "kebab", 
        "taco", "ramen", "maki", "sashimi", "tempura", "tofu", "caviale", 
        "ostriche", "gamberi", "calamari", "cozze", "vongole", "panna_cotta", 
        "sorbetto", "profiterole", "crostata", "spumante", "champagne", 
        "liquore", "grappa", "whiskey", "piadina", "calzone", "panzerotto", 
        "timballo", "porchetta", "spezzatino", "cotechino", "zampone", "baccala", 
        "astice", "aragosta", "granchio", "macarons", "muffin", "pancake", 
        "cheesecake", "cannoli", "cassata", "pandoro", "panettone"
    ]
             
    adjectives = [
        "al_pesto", "margherita", "ai_funghi", "al_forno", "fritto", 
        "alla_griglia", "al_limone", "piccante", "dolce", "salato", 
        "con_patate", "allo_scoglio", "tartufo", "speciale", "della_casa", 
        "affumicato", "bollito", "al_sugo", "in_bianco", "alla_carbonara", 
        "all_amatriciana", "cacio_e_pepe", "alla_norma", "ai_quattro_formaggi", 
        "capricciosa", "diavola", "boscaiola", "ortolana", "marinara", 
        "ripieno", "gratinato", "in_umido", "al_vapore", "crudo", "cotto", 
        "ben_cotto", "al_sangue", "impanato", "glassato", "caramellato", 
        "agrodolce", "speziato", "al_curry", "al_pepe_verde", "con_zucchine", 
        "con_melanzane", "con_peperoni", "al_radicchio", "alle_noci", 
        "al_pistacchio", "al_cioccolato", "ai_frutti_di_bosco", "alla_pizzaiola", 
        "alla_cacciatora", "al_vino_bianco", "al_vino_rosso", "al_gorgonzola", 
        "al_balsamico", "con_cipolle", "con_pomodorini", "alla_romana", 
        "alla_napoletana", "alla_milanese", "alla_siciliana", "alla_fiorentina", 
        "alla_genovese", "alla_bolognese", "alla_parmigiana", "alla_puttanesca", 
        "all_arrabbiata", "allo_zafferano", "alla_vaniglia"
    ]
    
    dishes = set()
    for b in bases:
        if len(dishes) < n: dishes.add(b)
    if len(dishes) < n:
        for b in bases:
            for a in adjectives:
                if len(dishes) < n: dishes.add(f"{b}_{a}")
    counter = 1
    while len(dishes) < n:
        b = random.choice(bases)
        a = random.choice(adjectives)
        dishes.add(f"{b}_{a}_{counter}")
        counter += 1
        
    lista_piatti = list(dishes)
    random.shuffle(lista_piatti)
    return lista_piatti

def main():
    # Configurazioni: (numero_piatti, numero_dipendenti, opzioni_max)
    configs = [
        (10, 20, 3),            # open1.txt - Molto piccolo (KB)
        (20, 100, 4),           # open2.txt - Piccolo
        (50, 500, 5),           # open3.txt - Medio-piccolo
        (100, 2500, 6),         # open4.txt - Medio
        (200, 10000, 8),        # open5.txt - ~ 1 MB
        (400, 25000, 10),       # open6.txt - ~ 3-4 MB
        (700, 50000, 12),       # open7.txt - ~ 9-10 MB
        (1000, 100000, 15),     # open8.txt - ~ 20-25 MB
        (2000, 250000, 20),     # open9.txt - ~ 70-80 MB
        (3000, 500000, 25)      # open10.txt - ~ 150+ MB (enorme, per stress test finale)
    ]
    
    print("Inizio generazione dei 10 file di test progressivi...")
    print("-" * 60)
    
    for i, (d, e, m) in enumerate(configs, 1):
        filename = f"open{i}.txt"
        print(f"Generazione {filename} in corso... (Piatti: {d}, Dipendenti: {e})")
        start_time = time.time()
        
        dishes = generate_dish_names(d)
        
        with open(filename, 'w') as f:
            f.write(" ".join(dishes) + "\n")
            
            for _ in range(e):
                num_options = random.randint(1, min(m, d))
                chosen_dishes = random.sample(dishes, num_options)
                
                prefs = []
                for dish in chosen_dishes:
                    sign = "-" if random.choice([True, False]) else ""
                    prefs.append(f"{sign}{dish}")
                
                f.write(" ".join(prefs) + "\n")
        
        size_mb = os.path.getsize(filename) / (1024 * 1024)
        elapsed = time.time() - start_time
        
        if size_mb < 1:
            size_str = f"{size_mb * 1024:.2f} KB"
        else:
            size_str = f"{size_mb:.2f} MB"
            
        print(f"✅ Creato {filename} in {elapsed:.2f}s | Dimensione: {size_str}")
        print("-" * 60)
        
    print("Generazione completata con successo! Tutti i 10 file sono pronti.")

if __name__ == '__main__':
    main()

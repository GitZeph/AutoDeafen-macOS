#pragma once

#include <string>
#include <functional>

// ---------------------------------------------------------------------------
// AutoDeafen - Mini server HTTP per il redirect OAuth
// ---------------------------------------------------------------------------
// Discord, dopo che l'utente autorizza, reindirizza il browser a
// http://localhost:8000/?code=XXXX . Qui mettiamo in ascolto un socket su
// 127.0.0.1:8000, leggiamo quella richiesta, estraiamo il "code" e lo passiamo
// alla callback. Restituiamo una paginetta HTML all'utente.
//
// Tutto avviene su un thread secondario: la funzione non blocca il chiamante.
// ---------------------------------------------------------------------------

namespace autodeafen {
namespace oauth {

    // Avvia (una sola volta) il server di redirect su 127.0.0.1:8000.
    // onResult viene chiamata con (code, error):
    //   - code valorizzato, error vuoto   -> autorizzazione riuscita
    //   - code vuoto, error valorizzato   -> errore (timeout, errore di Discord…)
    // La callback viene invocata su un thread secondario.
    void startRedirectServer(std::function<void(std::string code,
                                                std::string error)> onResult);

} // namespace oauth
} // namespace autodeafen

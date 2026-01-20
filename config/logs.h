#ifndef LOGS_H
#define LOGS_H

#include <string>

namespace Logs {

// Schematy logów
const std::string PETENT_CREATED = "[INFO] Petent {0} został utworzony.";
const std::string PETENT_ENTERED_BUILDING = "[INFO] Petent {0} wszedł do budynku.";
const std::string PETENT_RECEIVED_TICKET = "[INFO] Petent {0} otrzymał bilet do wydziału {1}.";
const std::string PETENT_CALLED_TO_OFFICER = "[INFO] Petent {0} został wezwany przez urzędnika {1}.";
const std::string PETENT_SERVED = "[INFO] Petent {0} został obsłużony przez urzędnika {1}.";
const std::string PETENT_SENT_TO_CASHIER = "[INFO] Petent {0} został skierowany do kasy.";
const std::string PETENT_REDIRECTED = "[INFO] Petent {0} został przekierowany do innego urzędnika.";
const std::string PETENT_REJECTED = "[WARNING] Petent {0} został odprawiony przez urzędnika {1}.";
const std::string PETENT_LEFT_BUILDING = "[INFO] Petent {0} opuścił budynek.";
const std::string PETENT_FRUSTRATED = "[ERROR] Petent {0} jest sfrustrowany i opuszcza budynek.";

const std::string OFFICER_EXHAUSTED = "[WARNING] Urzędnik {0} wyczerpał limit obsługiwanych petentów.";
const std::string OFFICER_CLOSED = "[INFO] Urzędnik {0} zakończył pracę.";

const std::string SYSTEM_START = "[INFO] System uruchomiony.";
const std::string SYSTEM_SHUTDOWN = "[INFO] System zamykany.";
const std::string SYSTEM_ERROR = "[ERROR] Wystąpił błąd: {0}.";

} // namespace Logs

#endif // LOGS_H

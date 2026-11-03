/*
 * portal.h — Portal web de configuracao (WebServer na porta 80).
 *
 * Acesso:
 *   - pelo ponto de acesso do despertador: rede "Despertador-XXXX",
 *     senha AP_PASSWORD, pagina abre sozinha (captive portal) ou em
 *     http://192.168.4.1
 *   - pela rede de casa: http://despertador.local ou pelo IP da tela REDE
 *
 * A pagina e uma so (portal_page.h) e conversa com uma API JSON pequena,
 * entao novas opcoes entram acrescentando um campo no HTML e uma chave
 * em /api/save.
 */
#ifndef PORTAL_H
#define PORTAL_H

void portalBegin();     // registra as rotas e sobe o servidor
void portalUpdate();    // atende clientes e o DNS do captive portal (loop)

#endif /* PORTAL_H */

FROM nginx:1.27-alpine

# Configuracion del sitio (sirve estaticos + proxy /api -> api:8000)
COPY nginx.conf /etc/nginx/conf.d/default.conf

# Credenciales de autenticacion basica (ver README: como regenerarlas)
COPY .htpasswd /etc/nginx/.htpasswd

# Contenido del dashboard
COPY html/ /usr/share/nginx/html/

# config.js.template -> config.js con CARTO_API_KEY sustituida. La imagen
# base de nginx ya trae en /docker-entrypoint.d/ un script que corre
# envsubst sobre todo lo que haya en NGINX_ENVSUBST_TEMPLATE_DIR y vuelca el
# resultado (quitando el sufijo .template) en NGINX_ENVSUBST_OUTPUT_DIR: se
# redirige esa salida a la carpeta de estaticos en vez del conf.d por
# defecto, para que config.js quede junto a index.html/app.js.
COPY templates/config.js.template /etc/nginx/templates/config.js.template
ENV NGINX_ENVSUBST_OUTPUT_DIR=/usr/share/nginx/html

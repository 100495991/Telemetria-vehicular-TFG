FROM nginx:1.27-alpine

# Configuracion del sitio (sirve estaticos + proxy /api -> api:8000)
COPY nginx.conf /etc/nginx/conf.d/default.conf

# Credenciales de autenticacion basica (ver README: como regenerarlas)
COPY .htpasswd /etc/nginx/.htpasswd

# Contenido del dashboard
COPY html/ /usr/share/nginx/html/

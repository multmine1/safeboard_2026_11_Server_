ARG BASE_IMAGE
FROM ${BASE_IMAGE}
ENV NGINX_ENVSUBST_OUTPUT_DIR=/tmp
RUN rm /etc/nginx/conf.d/default.conf
COPY nginx.conf /etc/nginx/nginx.conf
COPY portal.conf.template /etc/nginx/templates/portal.conf.template
COPY dist/ /usr/share/nginx/html/

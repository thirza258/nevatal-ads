# Builds the React dashboard and serves it behind nginx together with the API.
FROM node:22-alpine AS dashboard-build

WORKDIR /app
COPY apps/dashboard/package.json apps/dashboard/package-lock.json ./
RUN npm ci --no-audit --no-fund

COPY apps/dashboard/index.html apps/dashboard/vite.config.ts apps/dashboard/tsconfig.json \
     apps/dashboard/postcss.config.js apps/dashboard/tailwind.config.js ./
COPY apps/dashboard/src ./src
RUN npm run build

FROM nginx:1.27-alpine

RUN rm -f /etc/nginx/conf.d/default.conf
COPY deploy/nginx/default.conf /etc/nginx/conf.d/default.conf
COPY --from=dashboard-build /app/dist /usr/share/nginx/html
COPY agentdocs/.well-known /usr/share/nginx/html/.well-known
COPY agentdocs/auth.md agentdocs/llms.txt /usr/share/nginx/html/
COPY agentdocs/webmcp.js /usr/share/nginx/html/webmcp.js

EXPOSE 80

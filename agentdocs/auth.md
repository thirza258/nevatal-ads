# auth.md

## Overview
Authentication and registration guide for agents using AdControl Learning.

## Discovery
- OAuth Protected Resource Metadata: https://marketing.nevatal.id/.well-known/oauth-protected-resource
- OAuth 2.0 Authorization Server Metadata: https://marketing.nevatal.id/.well-known/oauth-authorization-server
- OpenID Connect Discovery: https://marketing.nevatal.id/.well-known/openid-configuration
- API Catalog: https://marketing.nevatal.id/.well-known/api-catalog
- Agent Card: https://marketing.nevatal.id/.well-known/agent-card.json

## Registration
Public content can be read without credentials. Agents that need write
access register at https://marketing.nevatal.id/api/agent/register and receive bearer credentials.

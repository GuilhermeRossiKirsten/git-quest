package catalog

// productKey monta a chave do produto no Redis.
//
// Os IDs vêm do ERP, que numera os produtos sequencialmente — então o ID
// sozinho já identifica o produto. (Escrito quando havia uma loja só.)
func productKey(tenant, id string) string {
	return "product:" + id
}

class Catalog:
    def __init__(self):
        self.items = {}

    def find_item(self, identifier):
        return self.items.get(identifier)

    def clear(self):
        self.items.clear()

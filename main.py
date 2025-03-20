# main.py

import time
import logging

from config import OPENAI_API_KEY, EMBEDDING_MODEL
from controllers import symbol_extractor
from controllers.index_builder import IndexBuilder
from services.embedding_service import EmbeddingService
from services.search import SearchService

def main():
    dir_path = input("Enter the path to your codebase directory: ").strip()

    start_time = time.time()
    symbols = symbol_extractor.extract_symbols_from_directory(dir_path)
    if not symbols:
        logging.error("No symbols found. Check the extraction method or the codebase contents.")
        return
    
    embedding_service = EmbeddingService(OPENAI_API_KEY, EMBEDDING_MODEL)
    index_builder = IndexBuilder(embedding_service, batch_size=1000)
    index = index_builder.build_index(symbols)

    end_time = time.time()
    logging.info(f"Index built in {end_time - start_time:.2f} seconds. Ready for search queries.")

    search_service = SearchService(embedding_service)
    while True:
        query = input("Enter search query (or 'exit' to quit): ")
        if query.lower() == 'exit':
            break
        results = search_service.search(query, index)
        print("\nTop results:")
        for score, item in results:
            print(f"Score: {score:.3f} | Type: {item['type']} | Name: {item['name']} | File: {item['file']}")
            print(item['name']+item['snippet'])
            print("-" * 40)

if __name__ == "__main__":
    main()

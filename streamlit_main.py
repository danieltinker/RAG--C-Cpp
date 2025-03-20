import streamlit as st
import time
import logging
import os

from config import OPENAI_API_KEY, EMBEDDING_MODEL
from controllers.symbol_extractor import extract_symbols_from_directory
from services.embedding_service import EmbeddingService
from controllers.index_builder import IndexBuilder
from services.search_service import SearchService

# Configure logging (optional)
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")

st.title("Codebase Semantic Search")

st.markdown("""
This application extracts symbols (like functions) from your codebase, builds an index using embeddings, and allows you to search for similar code snippets.
""")

# === Step 1: Build the Index ===
st.header("1. Build Index")
dir_path = st.text_input("Enter the path to your codebase directory:", value=".")

if st.button("Build Index"):
    if not os.path.isdir(dir_path):
        st.error("Directory not found. Please check the path.")
    else:
        with st.spinner("Extracting symbols and building index..."):
            start_time = time.time()
            symbols = extract_symbols_from_directory(dir_path)
            if not symbols:
                st.error("No symbols found. Check the extraction method or your codebase contents.")
            else:
                embedding_service = EmbeddingService(OPENAI_API_KEY, EMBEDDING_MODEL)
                index_builder = IndexBuilder(embedding_service, batch_size=1000)
                index = index_builder.build_index(symbols)
                st.session_state["index"] = index
                elapsed = time.time() - start_time
                st.success(f"Index built in {elapsed:.2f} seconds with {len(index)} snippets.")

# === Step 2: Search the Index ===
st.header("2. Search")
query = st.text_input("Enter your search query:")

# Add a numeric input for the number of top results (top_k)
top_k = st.number_input("Number of top results:", min_value=1, value=3, step=1)

if st.button("Search"):
    if "index" not in st.session_state:
        st.error("Index not built yet. Please build the index first.")
    elif not query.strip():
        st.error("Please enter a valid query.")
    else:
        embedding_service = EmbeddingService(OPENAI_API_KEY, EMBEDDING_MODEL)
        search_service = SearchService(embedding_service)
        results = search_service.search(query, st.session_state["index"], top_k=int(top_k))
        if not results:
            st.warning("No matching results found.")
        else:
            st.subheader("Top Results:")
            for score, item in results:
                st.markdown(f"**Score:** {score:.3f}")
                st.markdown(f"**Type:** {item['type']} | **Name:** {item['name']} | **File:** {item['file']}")
                st.code(item['name'] + ' ' + item['snippet'], language='cpp')
                st.markdown("---")
